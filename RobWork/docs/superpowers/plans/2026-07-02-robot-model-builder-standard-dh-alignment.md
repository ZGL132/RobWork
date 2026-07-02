# RobotModelBuilder Standard DH Alignment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make RobotModelBuilder interpret and preview its DH table using standard DH so the preview, exported XML, and loaded scene all match.

**Architecture:** Keep the exported DH representation unchanged as `type="schilling"` and treat that as the single source of truth for DH semantics. Fix only the DH preview vector computation and add focused regression tests that prove the preview now follows the standard DH translation implied by the exported RobWork transform.

**Tech Stack:** C++ in RobWorkStudio, Qt `QString`-based XML tests, CMake build target `sdurws_robotmodelbuilder_xmltest`, RobWork DH transform conventions.

---

## File Responsibilities

- `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
  - Holds the preview geometry math used by `computeLinkPose(...)` and `applyLinkGeometry(...)`.
- `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`
  - Holds the regression tests for XML output and preview link geometry.
- `D:/10_Source_Repos/21_robot/RobWork/RobWork/docs/RobotModelBuilderDynamics.md`
  - Explains how DH-mode preview geometry is derived for plugin users and future maintainers.

### Task 1: Add Failing Standard-DH Regression Tests

**Files:**
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`
- Test: `D:/10_Source_Repos/21_robot/RobWork/RobWork/build/Desktop_Qt_6_11_1_MSVC2022_64bit-Release/RobWorkStudio/bin/sdurws_robotmodelbuilder_xmltest.exe`

- [ ] **Step 1: Write the failing DH-specific tests**

Add a new block near the existing `applyLinkGeometry` checks that uses real DH data and asserts the standard-DH preview translation:

```cpp
    RobotModelSpec standardDh = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    standardDh.mode = RobotModelMode::DH;
    RobotModelXmlWriter::applyLinkGeometry (standardDh);

    bool foundDhLink = false;
    for (const DrawableSpec& d : standardDh.drawables) {
        if (QString::fromStdString (d.name) == "Link4To5") {
            foundDhLink = true;
            if (std::abs (d.length - 0.38) > 1e-6)
                return fail (QString ("Link4To5 length should be 0.38 in DH mode, got %1")
                                 .arg (d.length));
            if (std::abs (d.pos[0]) > 1e-6 || std::abs (d.pos[1]) > 1e-6 ||
                std::abs (d.pos[2] - 0.19) > 1e-6)
                return fail (QString ("Link4To5 pos should be 0 0 0.19 in standard DH, got %1 %2 %3")
                                 .arg (d.pos[0])
                                 .arg (d.pos[1])
                                 .arg (d.pos[2]));
            break;
        }
    }
    if (!foundDhLink)
        return fail ("Could not find Link4To5 drawable in DH mode.");

    RobotModelSpec offsetDh = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    offsetDh.mode = RobotModelMode::DH;
    offsetDh.dhJoints[1].alphaDeg = 0;
    offsetDh.dhJoints[1].a = 0.4;
    offsetDh.dhJoints[1].d = 0.2;
    offsetDh.dhJoints[1].offsetDeg = 90;
    RobotModelXmlWriter::applyLinkGeometry (offsetDh);

    bool foundOffsetLink = false;
    for (const DrawableSpec& d : offsetDh.drawables) {
        if (QString::fromStdString (d.name) == "Link1To2") {
            foundOffsetLink = true;
            if (std::abs (d.length - std::sqrt (0.4 * 0.4 + 0.2 * 0.2)) > 1e-6)
                return fail ("Link1To2 length should follow standard DH XY/Z projection.");
            if (std::abs (d.pos[0]) > 1e-6 || std::abs (d.pos[1] - 0.2) > 1e-6 ||
                std::abs (d.pos[2] - 0.1) > 1e-6)
                return fail (QString ("Link1To2 pos should be 0 0.2 0.1 for 90 deg offset, got %1 %2 %3")
                                 .arg (d.pos[0])
                                 .arg (d.pos[1])
                                 .arg (d.pos[2]));
            break;
        }
    }
    if (!foundOffsetLink)
        return fail ("Could not find Link1To2 drawable for offset DH case.");
```

- [ ] **Step 2: Run the test binary to verify it fails for the expected reason**

Run:

```powershell
& 'D:/10_Source_Repos/21_robot/RobWork/RobWork/build/Desktop_Qt_6_11_1_MSVC2022_64bit-Release/RobWorkStudio/bin/sdurws_robotmodelbuilder_xmltest.exe'
```

Expected:

```text
FAIL ... Link4To5 pos should be 0 0 0.19 in standard DH ...
```

### Task 2: Implement the Standard-DH Preview Fix And Docs

**Files:**
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/docs/RobotModelBuilderDynamics.md`
- Test: `D:/10_Source_Repos/21_robot/RobWork/RobWork/build/Desktop_Qt_6_11_1_MSVC2022_64bit-Release/RobWorkStudio/bin/sdurws_robotmodelbuilder_xmltest.exe`

- [ ] **Step 1: Change DH preview vector math to standard DH**

Replace the existing DH helper in `RobotModelXmlWriter.cpp`:

```cpp
void dhLinkVector (double alphaDeg, double a, double d, std::array< double, 3 >& v)
{
    const double alpha = alphaDeg * Pi / 180.0;
    v[0] = a;
    v[1] = -d * std::sin (alpha);
    v[2] = d * std::cos (alpha);
}
```

with a standard-DH helper that follows the exported `schilling` transform and includes joint offset:

```cpp
void dhLinkVector (double a, double d, double offsetDeg, std::array< double, 3 >& v)
{
    const double theta = offsetDeg * Pi / 180.0;
    v[0]               = a * std::cos (theta);
    v[1]               = a * std::sin (theta);
    v[2]               = d;
}
```

Then update the DH call site in `computeLinkPose(...)` from:

```cpp
        const DHJointSpec& j = spec.dhJoints[jointIdx];
        dhLinkVector (j.alphaDeg, j.a, j.d, v);
```

to:

```cpp
        const DHJointSpec& j = spec.dhJoints[jointIdx];
        dhLinkVector (j.a, j.d, j.offsetDeg, v);
```

- [ ] **Step 2: Update the documentation sentence to standard DH**

Change the DH-mode explanation in `docs/RobotModelBuilderDynamics.md` from the Craig-style sentence:

```text
DH 模式用 `RotX(α)·TransX(a)·TransZ(d)`
```

to:

```text
DH 模式按标准 DH（与导出的 `type="schilling"` 一致）使用 `RotZ(theta)·TransZ(d)·TransX(a)·RotX(α)`，在预览零位时由 `a*cos(theta)`, `a*sin(theta)`, `d` 反算相邻关节位移
```

- [ ] **Step 3: Build the XML test target**

Run:

```powershell
cmake --build 'D:/10_Source_Repos/21_robot/RobWork/RobWork/build/Desktop_Qt_6_11_1_MSVC2022_64bit-Release' --target sdurws_robotmodelbuilder_xmltest --config Release
```

Expected:

```text
Build succeeded
```

- [ ] **Step 4: Run the XML test binary and verify green**

Run:

```powershell
& 'D:/10_Source_Repos/21_robot/RobWork/RobWork/build/Desktop_Qt_6_11_1_MSVC2022_64bit-Release/RobWorkStudio/bin/sdurws_robotmodelbuilder_xmltest.exe'
```

Expected:

```text
exit code 0
```

- [ ] **Step 5: Review the diff for scope control**

Run:

```powershell
git diff -- RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp docs/RobotModelBuilderDynamics.md
```

Expected:

```text
Only the DH preview helper/call site, DH regression tests, and DH documentation text changed
```

## Self-Review

- Spec coverage:
  - Standard-DH preview alignment is covered by Task 1 and Task 2 Step 1.
  - Keeping XML `type="schilling"` unchanged is enforced by changing no XML writer output path.
  - Documentation alignment is covered by Task 2 Step 2.
  - Verification is covered by Task 2 Steps 3-5.
- Placeholder scan:
  - No `TODO`, `TBD`, or deferred behavior remains.
- Type consistency:
  - The plan changes only the existing `dhLinkVector(...)` helper signature and its single call site, so there are no new public API names to drift.

