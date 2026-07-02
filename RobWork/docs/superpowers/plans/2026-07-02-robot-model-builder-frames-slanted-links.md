# RobotModelBuilder Frames and Slanted Links Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make RobotModelBuilder generate RobWork XML with explicit Base/TCP frames and automatically oriented cylindrical links that correctly connect joints with combined X/Y/Z offsets.

**Architecture:** Keep the plugin UI and XML writer model-driven. Extend the internal model with fixed frames and generated link drawables, then compute each cylindrical link from the parent joint frame to the next joint origin using vector math instead of fixed X-only or Z-only assumptions.

**Tech Stack:** C++17-style code in RobWorkStudio, Qt QString/QTextStream tests, RobWork XML device format, CMake/Ninja/MSVC/Qt Creator.

---

## Current Findings

The UR reference file `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/UR.wc.xml` uses explicit frames for several roles:

- `Base`: a stable device root frame.
- `BaseMov`: a movable auxiliary frame.
- `DH0` to `DH5`: intermediate movable frames carrying DH properties.
- `TCP`: a tool/end frame attached to the last joint.
- `Drawable` elements reference frames and joints explicitly.

The current plugin-generated file `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/example/ModelData/XMLDevices/GenericSixAxis.wc.xml` can load as a simplified serial device, but it has limitations:

- In DH mode it emits only six `<DHJoint>` elements, limits, poses, and drawables. It does not emit a `Base` frame or a `TCP` frame.
- In Joint + RPY + Pos mode it emits six `<Joint>` elements, but still does not emit explicit `Base` or `TCP` frames.
- Default link drawables are generated in `RobotModelXmlWriter.cpp::appendLinks()` with hard-coded lengths and either `RPY 0 90 0` for X links or `RPY 0 0 0` for Z links.
- If `Joint2` relative to `Joint1` has `Pos 0.12 0 0.12`, the correct visual link is a diagonal cylinder from `(0,0,0)` to `(0.12,0,0.12)` in the `Joint1` frame. The current code still places an X-only cylinder at `Pos 0.06 0 0`, so the visual link does not connect the actual joint origins.

There is also an existing more complete handwritten example at `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/example/ModelData/XMLDevices/GenericSixAxis/GenericSixAxis.wc.xml`. It already demonstrates a better baseline: `Base`, `Joint1..Joint6`, `TCP`, base pedestal, joint axis cylinders, link cylinders, limits, and poses. Use it as a formatting and structure reference, but do not copy hard-coded link geometry logic back into the generator.

## Recommended Design

Use explicit frames:

- Always emit `<Frame name="Base">` before the joint chain.
- Always emit `<Frame name="TCP" refframe="Joint6">` after the last joint.
- Add `RobotModelSpec::baseFrame` and `RobotModelSpec::tcpFrame`, or a small `FrameSpec` vector if future frames are expected.
- Keep the serial device self-contained; the scene file should remain a wrapper with `RobotBase` and `<Include file="...">`.

Use automatic generated link drawables:

- Keep user-editable Drawable rows for manual geometry.
- Add an automatic generated-link path for the default cylinders.
- For each link from `Joint i` to `Joint i+1`, compute from the next joint's local `Pos` vector, because a drawable referenced to `Joint i` should connect from the current joint origin to the next joint origin.
- Cylinder primitives are defined along local +Z by default. To align a cylinder with an arbitrary vector `v = (x, y, z)`, compute:
  - `length = norm(v)`
  - `pos = v / 2`
  - `rpyDeg = rotation that maps local +Z to normalized(v)`
- If the length is close to zero, skip the link drawable or generate a very short visual stub only if explicitly requested.

Use a test-first implementation:

- Add tests that fail with the current hard-coded link logic.
- Add tests for Base/TCP frames.
- Add tests for a diagonal `Joint2` position of `0.12 0 0.12`; expected link length is about `0.169705627484771`, position is `0.06 0 0.06`, and rotation is about `0 45 0` if using the existing RPY convention for +Z to +X as `0 90 0`.

## Files To Modify

- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`
  - Add a `FrameSpec` struct for Base/TCP metadata.
  - Add `baseFrame` and `tcpFrame` fields to `RobotModelSpec`, or add `std::vector<FrameSpec> frames`.

- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`
  - Expose small testable helpers if needed, such as `makeLinkDrawable()` or `linkCylinderFromVector()`.

- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
  - Emit Base/TCP frames.
  - Replace `appendLinks()` hard-coded lengths/orientations with vector-derived link generation.
  - Add helper functions for vector norm, radians/degrees, and RPY generation.

- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`
  - Add failing tests for Base/TCP and diagonal link geometry.

- Optional Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
  - Only if the UI needs new controls for TCP offset or automatic link generation. Keep this optional for the first fix.

## Task 1: Add XML Tests For Base, TCP, And Diagonal Link

**Files:**
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add helper checks for numeric substrings**

Add these helpers near the existing `contains()` helper:

```cpp
bool containsAny (const QString& text, const QStringList& needles)
{
    for (const QString& needle : needles) {
        if (text.contains (needle))
            return true;
    }
    return false;
}
```

- [ ] **Step 2: Add failing assertions for Base and TCP frames**

After `const QString serialXml = ...`, add:

```cpp
    if (!contains (serialXml, "<Frame name=\"Base\""))
        return fail ("Serial XML missing Base frame.");
    if (!contains (serialXml, "<Frame name=\"TCP\" refframe=\"Joint6\""))
        return fail ("Serial XML missing TCP frame attached to Joint6.");
```

Expected current result before implementation: FAIL with `Serial XML missing Base frame.`

- [ ] **Step 3: Add failing diagonal link test**

After the existing scene include check, add:

```cpp
    RobotModelSpec diagonalSpec =
        RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    diagonalSpec.mode = RobotModelMode::JointRPYPos;
    diagonalSpec.transformJoints[1].pos = {{0.12, 0, 0.12}};
    diagonalSpec.drawables.clear ();
    RobotModelXmlWriter::appendGeneratedDrawables (diagonalSpec);

    const QString diagonalXml = RobotModelXmlWriter::makeSerialDeviceXml (diagonalSpec);
    if (!contains (diagonalXml, "<Drawable name=\"Link1To2\" refframe=\"Joint1\""))
        return fail ("Diagonal XML missing Link1To2 drawable.");
    if (!contains (diagonalXml, "<Pos>0.06 0 0.06</Pos>"))
        return fail ("Diagonal Link1To2 should be centered between Joint1 and Joint2.");
    if (!containsAny (diagonalXml,
                      QStringList () << "<Cylinder radius=\"0.055\" z=\"0.169705627484771\" />"
                                     << "<Cylinder radius=\"0.055\" z=\"0.16970562748477\" />"))
        return fail ("Diagonal Link1To2 should use vector length.");
    if (!containsAny (diagonalXml, QStringList () << "<RPY>0 45 0</RPY>"
                                                  << "<RPY>0 45.0000000000000 0</RPY>"))
        return fail ("Diagonal Link1To2 should rotate cylinder toward the offset vector.");
```

If `appendGeneratedDrawables()` does not exist yet, declare it in Task 2. This test should initially fail to compile, which is acceptable as the RED state for the new API.

- [ ] **Step 4: Run the test target**

Run:

```powershell
$bat = Join-Path $env:TEMP 'rmb_xmltest_red.bat'
Set-Content -LiteralPath $bat -Encoding ASCII -Value @'
@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --config Release --target sdurws_robotmodelbuilder_xmltest
exit /b %ERRORLEVEL%
'@
cmd /c "$bat"
```

Expected: build or test failure caused by missing Base/TCP output or missing `appendGeneratedDrawables()`.

- [ ] **Step 5: Commit the failing test**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "Test RobotModelBuilder frames and slanted links"
```

## Task 2: Add FrameSpec And Public Drawable Regeneration API

**Files:**
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`

- [ ] **Step 1: Add FrameSpec**

In `RobotModelSpec.hpp`, add before `DHJointSpec`:

```cpp
struct FrameSpec
{
    std::string name;
    std::string refFrame;
    std::string type;
    std::array< double, 3 > rpyDeg;
    std::array< double, 3 > pos;
    bool showAxis;
};
```

In `RobotModelSpec`, add:

```cpp
    FrameSpec baseFrame;
    FrameSpec tcpFrame;
```

- [ ] **Step 2: Add public drawable regeneration declaration**

In `RobotModelXmlWriter.hpp`, add to the public section:

```cpp
    static void appendGeneratedDrawables (RobotModelSpec& spec);
```

- [ ] **Step 3: Initialize Base and TCP defaults**

In `makeDefaultSixAxisModel()`, after setting `generateScene`, add:

```cpp
    spec.baseFrame.name     = "Base";
    spec.baseFrame.refFrame = "";
    spec.baseFrame.type     = "";
    spec.baseFrame.rpyDeg   = {{0, 0, 0}};
    spec.baseFrame.pos      = {{0, 0, 0}};
    spec.baseFrame.showAxis = true;

    spec.tcpFrame.name     = "TCP";
    spec.tcpFrame.refFrame = "Joint6";
    spec.tcpFrame.type     = "";
    spec.tcpFrame.rpyDeg   = {{0, 0, 0}};
    spec.tcpFrame.pos      = {{0, 0, 0.12}};
    spec.tcpFrame.showAxis = true;
```

- [ ] **Step 4: Rename internal generated drawable function**

Rename the old internal calls:

```cpp
    appendJointAxes (spec);
    appendLinks (spec);
```

to:

```cpp
    appendGeneratedDrawables (spec);
```

Add implementation:

```cpp
void RobotModelXmlWriter::appendGeneratedDrawables (RobotModelSpec& spec)
{
    appendJointAxes (spec);
    appendLinks (spec);
}
```

This can be minimal for Task 2; Task 3 replaces `appendLinks()` internals.

- [ ] **Step 5: Run test target**

Run the same `sdurws_robotmodelbuilder_xmltest` build command.

Expected: compile succeeds, tests still fail on missing XML frame emission or diagonal link assertions.

- [ ] **Step 6: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp
git commit -m "Add RobotModelBuilder frame specs"
```

## Task 3: Emit Base And TCP Frames

**Files:**
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`

- [ ] **Step 1: Add frame writer helper**

In the anonymous namespace of `RobotModelXmlWriter.cpp`, add:

```cpp
void writeFrame (QTextStream& out, const FrameSpec& frame)
{
    out << "  <Frame name=\"" << QString::fromStdString (frame.name) << "\"";
    if (!frame.refFrame.empty ())
        out << " refframe=\"" << QString::fromStdString (frame.refFrame) << "\"";
    if (!frame.type.empty ())
        out << " type=\"" << QString::fromStdString (frame.type) << "\"";
    out << ">\n";
    out << "    <RPY>" << RobotModelXmlWriter::vector3 (frame.rpyDeg) << "</RPY>\n";
    out << "    <Pos>" << RobotModelXmlWriter::vector3 (frame.pos) << "</Pos>\n";
    if (frame.showAxis)
        out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
    out << "  </Frame>\n\n";
}
```

If `vector3()` is private, either move this helper into class implementation where it can access `vector3()`, or keep the helper near `makeSerialDeviceXml()` as a small local lambda.

- [ ] **Step 2: Emit Base before joints**

At the start of `makeSerialDeviceXml()` after the `<SerialDevice>` line, add:

```cpp
    writeFrame (out, spec.baseFrame);
```

- [ ] **Step 3: Emit TCP after joints and before drawables**

After the DH/Joint block and before `if (spec.generateDrawables)`, add:

```cpp
    writeFrame (out, spec.tcpFrame);
```

- [ ] **Step 4: Run test target**

Run:

```powershell
cmd /c "$env:TEMP\rmb_xmltest_red.bat"
```

Expected: Base/TCP assertions pass; diagonal link assertions still fail until Task 4.

- [ ] **Step 5: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp
git commit -m "Emit RobotModelBuilder base and TCP frames"
```

## Task 4: Generate Slanted Link Cylinders From Joint Offsets

**Files:**
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`

- [ ] **Step 1: Add vector math helpers**

In the anonymous namespace, add:

```cpp
constexpr double Pi = 3.14159265358979323846;

double radToDeg (double value)
{
    return value * 180.0 / Pi;
}

double norm3 (const std::array< double, 3 >& v)
{
    return std::sqrt (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}
```

Also add:

```cpp
#include <cmath>
```

- [ ] **Step 2: Add +Z-to-vector RPY helper**

Use the same convention already implied by the plugin: cylinder local +Z rotated by `RPY 0 90 0` points along +X.

```cpp
std::array< double, 3 > cylinderRpyFromVector (const std::array< double, 3 >& v)
{
    const double length = norm3 (v);
    if (length <= 1e-9)
        return {{0, 0, 0}};

    const double x = v[0] / length;
    const double y = v[1] / length;
    const double z = v[2] / length;

    const double yaw   = std::atan2 (y, x);
    const double pitch = std::atan2 (std::sqrt (x * x + y * y), z);

    return {{0, radToDeg (pitch), radToDeg (yaw)}};
}
```

Important: This helper is intentionally minimal. It maps +Z to the target direction using yaw around Z and pitch around Y in the plugin's current RPY convention. Verify visually in RobWorkStudio; if Y direction is inverted, flip the sign of yaw or pitch in one follow-up commit with a targeted test.

- [ ] **Step 3: Add helper to get relative link vectors**

Add:

```cpp
std::array< double, 3 > linkVectorForIndex (const RobotModelSpec& spec, int linkIndex)
{
    if (spec.mode == RobotModelMode::JointRPYPos && linkIndex + 1 < (int) spec.transformJoints.size ())
        return spec.transformJoints[linkIndex + 1].pos;

    if (spec.mode == RobotModelMode::DH && linkIndex + 1 < (int) spec.dhJoints.size ()) {
        const DHJointSpec& next = spec.dhJoints[linkIndex + 1];
        return {{next.a, 0, next.d}};
    }

    return {{0, 0, 0}};
}
```

Note: DH mode geometry is only approximate because real DH transforms also include `alpha` and offset. This plan keeps the visual link generation simple and consistent with the current plugin's simplified DH-to-position mapping. A later enhancement can compute full DH transforms if needed.

- [ ] **Step 4: Replace `appendLinks()` implementation**

Replace the old hard-coded `lengths` and `rpyDeg` logic with:

```cpp
void appendLinks (RobotModelSpec& spec)
{
    const double radii[5] = {0.055, 0.05, 0.045, 0.04, 0.035};
    for (int i = 0; i < 5; ++i) {
        const std::array< double, 3 > vector = linkVectorForIndex (spec, i);
        const double length                  = norm3 (vector);
        if (length <= 1e-9)
            continue;

        DrawableSpec drawable;
        drawable.name     = "Link" + std::to_string (i + 1) + "To" + std::to_string (i + 2);
        drawable.refFrame = "Joint" + std::to_string (i + 1);
        drawable.shape    = "Cylinder";
        drawable.radius   = radii[i];
        drawable.length   = length;
        drawable.rpyDeg   = cylinderRpyFromVector (vector);
        drawable.pos      = {{vector[0] / 2.0, vector[1] / 2.0, vector[2] / 2.0}};
        drawable.rgb      = {{0.35, 0.45, 0.65}};
        drawable.collisionModel = false;
        spec.drawables.push_back (drawable);
    }
}
```

- [ ] **Step 5: Run test target**

Run:

```powershell
cmd /c "$env:TEMP\rmb_xmltest_red.bat"
```

Expected: `sdurws_robotmodelbuilder_xmltest` passes.

- [ ] **Step 6: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp
git commit -m "Generate slanted RobotModelBuilder link drawables"
```

## Task 5: Refresh Drawable Defaults When Kinematics Change

**Files:**
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Optional Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`

- [ ] **Step 1: Decide UX rule**

Use this rule for the first implementation:

- `Generate Preview`, `Save XML`, and `Save and Load` should regenerate default joint-axis and link drawables from the current kinematics only when the Drawables table still contains the default generated names.
- If the user manually edits or adds drawables, do not overwrite those rows silently.

- [ ] **Step 2: Add helper to detect generated drawables**

In `RobotModelBuilderWidget.cpp`, add a local helper:

```cpp
bool isGeneratedDrawableName (const QString& name)
{
    return name.startsWith ("Joint") && name.endsWith ("Axis") ||
           name.startsWith ("Link") && name.contains ("To");
}
```

- [ ] **Step 3: Regenerate default drawables in `collectSpec()`**

After collecting `spec.drawables`, add:

```cpp
    bool onlyGeneratedDrawables = true;
    for (const DrawableSpec& drawable : spec.drawables) {
        if (!isGeneratedDrawableName (QString::fromStdString (drawable.name))) {
            onlyGeneratedDrawables = false;
            break;
        }
    }
    if (onlyGeneratedDrawables) {
        spec.drawables.clear ();
        RobotModelXmlWriter::appendGeneratedDrawables (spec);
    }
```

This keeps the Drawables table useful while making kinematics-driven preview correct for the common case.

- [ ] **Step 4: Run test target and build plugin**

Run:

```powershell
$bat = Join-Path $env:TEMP 'rmb_build_after_widget.bat'
Set-Content -LiteralPath $bat -Encoding ASCII -Value @'
@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --config Release --target sdurws_robotmodelbuilder_xmltest
if errorlevel 1 exit /b %ERRORLEVEL%
cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --config Release --target RobWorkStudio
exit /b %ERRORLEVEL%
'@
cmd /c "$bat"
```

Expected: both targets build successfully.

- [ ] **Step 5: Manual UI verification**

In Qt Creator:

1. Run `RobWorkStudio`.
2. Open `Plugins -> RobotModelBuilder`.
3. Set `Mode` to `Joint + RPY + Pos`.
4. Set `Joint2 Pos m` to `0.12 0 0.12`.
5. Click `Generate Preview`.
6. Confirm XML contains:
   - `<Frame name="Base">`
   - `<Frame name="TCP" refframe="Joint6">`
   - `Link1To2` with `Pos 0.06 0 0.06`
   - `Cylinder ... z="0.169705627..."`
7. Click `Save and Load`.
8. Confirm the first link is diagonal in the 3D view and visually connects Joint1 to Joint2.

- [ ] **Step 6: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp
git commit -m "Refresh RobotModelBuilder generated drawables"
```

## Task 6: Optional CollisionSetup Generation

**Files:**
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

Only do this task if collision model workflows are needed now. It is not required to fix the slanted visual links.

- [ ] **Step 1: Add option**

Add to `RobotModelSpec`:

```cpp
    bool generateCollisionSetup;
```

Default it to `false` in `makeDefaultSixAxisModel()`.

- [ ] **Step 2: Emit CollisionSetup reference**

In `makeSerialDeviceXml()`, before poses:

```cpp
    if (spec.generateCollisionSetup)
        out << "  <CollisionSetup file=\"CollisionSetup.xml\" />\n";
```

- [ ] **Step 3: Add writer**

Add:

```cpp
QString RobotModelXmlWriter::makeCollisionSetupXml (const RobotModelSpec& spec)
{
    QString xml;
    QTextStream out (&xml);
    out << "<CollisionSetup>\n";
    out << "  <Exclude>\n";
    out << "    <FramePair first=\"Base\" second=\"Joint1\"/>\n";
    for (int i = 1; i < 6; ++i)
        out << "    <FramePair first=\"Joint" << i << "\" second=\"Joint" << (i + 1) << "\"/>\n";
    out << "  </Exclude>\n";
    out << "</CollisionSetup>\n";
    return xml;
}
```

- [ ] **Step 4: Save file**

In `saveFiles()`, after scene generation, save `CollisionSetup.xml` when `generateCollisionSetup` is true.

- [ ] **Step 5: Test**

Add tests checking `<CollisionSetup file="CollisionSetup.xml" />` and adjacent frame pairs.

- [ ] **Step 6: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder
git commit -m "Add RobotModelBuilder collision setup output"
```

## Final Verification

Run:

```powershell
$bat = Join-Path $env:TEMP 'rmb_final_verify.bat'
Set-Content -LiteralPath $bat -Encoding ASCII -Value @'
@echo off
call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --config Release --target sdurws_robotmodelbuilder_xmltest
if errorlevel 1 exit /b %ERRORLEVEL%
cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --config Release --target RobWorkStudio
exit /b %ERRORLEVEL%
'@
cmd /c "$bat"
```

Expected:

- `sdurws_robotmodelbuilder_xmltest` builds and exits with code 0.
- `RobWorkStudio` builds and exits with code 0.
- Generated XML contains Base and TCP frames.
- A `Joint2` offset of `0.12 0 0.12` generates a diagonal `Link1To2` cylinder centered at `0.06 0 0.06`, length about `0.169705627484771`, and RPY about `0 45 0`.

## Git Hygiene

Do not commit user-generated XML files unless they are intentionally curated examples. In the current workspace, these files are untracked and should be left alone unless explicitly requested:

- `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/example/ModelData/XMLDevices/GenericSixAxis.wc.xml`
- `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/example/ModelData/XMLDevices/GenericSixAxisScene.wc.xml`

If the implementation creates temporary generated XML files during manual verification, either write them outside the repo or keep them untracked.

