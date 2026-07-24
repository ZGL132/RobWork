# RobotModelBuilder Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Harden the RobotModelBuilder plugin against malformed XML, uninitialized model data, unsafe scene references, surprising URDF import side effects, and unregistered tests.

**Architecture:** Keep the existing boundaries: `RobotModelSpec` remains the data model, `RobotModelXmlWriter` owns validation and serialization, `RobotModelBuilderWidget` owns user workflow, and CMake owns test registration. Do not restructure the plugin into new large modules; add narrow helpers and focused tests.

**Tech Stack:** C++/Qt Widgets, QtCore `QString`/`QTextStream`, RobWorkStudio plugin CMake, existing command-line regression executable `sdurws_robotmodelbuilder_xmltest`.

---

## Files And Responsibilities

- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`
  - Add small public validation helpers only if tests need them; prefer private helpers in `.cpp`.
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
  - Add XML escaping helpers.
  - Use escaping for all attribute and text values originating from names, paths, material IDs, patterns, and include files.
  - Add scene frame dependency validation.
  - Remove the stale `#if 0` implementation block.
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`
  - Add default initializers to `DrawableSpec`.
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
  - Make URDF import fill the UI and preview only; do not auto-save or auto-load.
  - Remove duplicate include/allocation noise.
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`
  - Register `sdurws_robotmodelbuilder_xmltest` with CTest.
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`
  - Add regression tests for XML escaping, default initialization, scene frame cycles/forward refs, and import workflow-adjacent behavior where possible.
- Modify: `docs/RobotModelBuilder.md`
  - Update stale statements about fixed 6 joints and URDF import side effects.

Recommended verification commands for each task:

```powershell
cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
ctest --test-dir "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" -C Release -R sdurws_robotmodelbuilder_xmltest --output-on-failure
```

Expected successful test executable output includes exit code `0`. Expected successful CTest output includes `100% tests passed` or one listed test passing.

---

### Task 1: Escape XML Attribute And Text Values

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add failing XML escaping regression test**

Insert this block in `RobotModelXmlWriterTest.cpp` after the default XML include checks around the current default model assertions:

```cpp
    // ---- XML escaping: user-controlled names, paths, and text must not break XML ----
    {
        RobotModelSpec escaping = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        escaping.robotName = "EscapingRobot";
        escaping.transformJoints[0].name = "Joint_A";
        escaping.drawables.clear ();
        DrawableSpec drawable;
        drawable.name = "Body & \"cover\"";
        drawable.refFrame = "Joint_A";
        drawable.shape = "Mesh";
        drawable.filePath = "meshes/part & \"quoted\".stl";
        drawable.dimensions = {{0.1, 0.1, 0.1}};
        drawable.radius = 0.05;
        drawable.length = 0.1;
        drawable.rpyDeg = {{0, 0, 0}};
        drawable.pos = {{0, 0, 0}};
        drawable.rgb = {{0.4, 0.5, 0.6}};
        drawable.collisionModel = false;
        escaping.drawables.push_back (drawable);
        escaping.dynamics.baseMaterial = "Steel & Aluminum";

        QStringList escapingErrors;
        if (!RobotModelXmlWriter::validate (escaping, escapingErrors))
            return fail ("Escaping regression spec should validate: " + escapingErrors.join ("; "));

        const QString escapingSerial = RobotModelXmlWriter::makeSerialDeviceXml (escaping);
        const QString escapingDwc = RobotModelXmlWriter::makeDynamicWorkCellXml (escaping);
        if (!contains (escapingSerial, "Body &amp; &quot;cover&quot;"))
            return fail ("Drawable name must be XML-escaped in SerialDevice output.");
        if (!contains (escapingSerial, "meshes/part &amp; &quot;quoted&quot;.stl"))
            return fail ("Drawable mesh path must be XML-escaped in SerialDevice output.");
        if (!contains (escapingDwc, "<MaterialID>Steel &amp; Aluminum</MaterialID>"))
            return fail ("DWC material text must be XML-escaped.");
    }
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
```

Expected: executable exits `1` with one of the new escaping failure messages.

- [ ] **Step 3: Add escaping helpers**

In `RobotModelXmlWriter.cpp`, add these helpers inside the anonymous namespace near `exportedRobotName`:

```cpp
QString xmlEscaped (const QString& value)
{
    QString escaped = value;
    escaped.replace ('&', "&amp;");
    escaped.replace ('<', "&lt;");
    escaped.replace ('>', "&gt;");
    escaped.replace ('"', "&quot;");
    escaped.replace ('\'', "&apos;");
    return escaped;
}

QString xmlEscaped (const std::string& value)
{
    return xmlEscaped (QString::fromStdString (value));
}
```

- [ ] **Step 4: Use escaping at every string interpolation point**

In `RobotModelXmlWriter.cpp`, update string output sites so numeric vectors still use `number()`/`vector3()`, while string values pass through `xmlEscaped`.

Minimum replacements:

```cpp
out << "<SerialDevice name=\"" << xmlEscaped (exportedRobotName (spec)) << "\">\n";
out << "  <DHJoint name=\"" << xmlEscaped (QString::fromStdString (joint.name)) << "\" alpha=\"";
out << "  <Frame name=\"" << xmlEscaped (QString::fromStdString (joint.name))
    << "\" refframe=\"" << xmlEscaped (refframe) << "\">\n";
out << "  <Joint name=\"" << xmlEscaped (QString::fromStdString (joint.name))
    << "\" type=\"" << xmlEscaped (QString::fromStdString (joint.type)) << "\">\n";
out << "  <PosLimit refjoint=\"" << xmlEscaped (QString::fromStdString (limit.jointName));
out << "  <VelLimit refjoint=\"" << xmlEscaped (QString::fromStdString (limit.jointName));
out << "  <AccLimit refjoint=\"" << xmlEscaped (QString::fromStdString (limit.jointName));
out << "  <Q name=\"" << xmlEscaped (QString::fromStdString (pose.name)) << "\">";
out << "<WorkCell name=\"" << xmlEscaped (robotName) << "Scene\">\n";
out << "  <Include file=\"" << xmlEscaped (robotName + ".wc.xml") << "\" />\n";
out << "  <CollisionSetup file=\"" << xmlEscaped (rel) << "\" />\n";
out << "  <ProximitySetup file=\"" << xmlEscaped (rel) << "\" />\n";
out << "  <Include file=\"" << xmlEscaped (rel) << "\" />\n";
out << "<DynamicWorkCell workcell=\"" << xmlEscaped (robotName + "Scene.wc.xml") << "\">\n";
out << "  <RigidDevice device=\"" << xmlEscaped (robotName) << "\">\n";
out << "    <ForceLimit joint=\"" << xmlEscaped (QString::fromStdString (fl.jointName)) << "\">";
out << "    <KinematicBase frame=\"" << xmlEscaped (QString::fromStdString (spec.dynamics.baseFrame)) << "\">\n";
out << "      <MaterialID>" << xmlEscaped (spec.dynamics.baseMaterial) << "</MaterialID>\n";
out << "    <Link object=\"" << xmlEscaped (QString::fromStdString (link.objectName)) << "\">\n";
```

Also update helper methods:

```cpp
out << "  <Frame name=\"" << xmlEscaped (frame.name)
    << "\" refframe=\"" << xmlEscaped (frame.refFrame) << "\""
    << frameTypeAttribute (frame.frameType);
```

```cpp
return QString ("<STL file=\"%1\" />").arg (xmlEscaped (geometry.file));
return QString ("<Mesh file=\"%1\" />").arg (xmlEscaped (geometry.file));
return QString ("<Polytope file=\"%1\" />").arg (xmlEscaped (geometry.file));
```

```cpp
out << "  <Drawable name=\"" << xmlEscaped (geometry.name)
    << "\" refframe=\"" << xmlEscaped (geometry.refFrame) << "\"";
out << "  <Drawable name=\"" << xmlEscaped (drawable.name)
    << "\" refframe=\"" << xmlEscaped (drawable.refFrame) << "\"";
return QString ("<Polytope file=\"%1\" />")
    .arg (xmlEscaped (relativeGeometryPath (spec, drawable.filePath)));
out << "  <CollisionModel name=\"" << xmlEscaped (collision.name)
    << "\" refframe=\"" << xmlEscaped (collision.refFrame) << "\">\n";
return QString ("<Polytope file=\"%1\" />")
    .arg (xmlEscaped (relativeGeometryPath (spec, collision.filePath)));
```

For `makeCollisionSetupXml()` and `makeProximitySetupXml()`, escape `pair.first`, `pair.second`, volatile frame names, `patternA`, and `patternB`.

- [ ] **Step 5: Run regression test**

Run the build and executable command from Step 2.

Expected: executable exits `0`.

- [ ] **Step 6: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "fix: escape RobotModelBuilder XML output"
```

---

### Task 2: Default-Initialize DrawableSpec

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add failing default initialization test**

Insert this block after the XML escaping test from Task 1:

```cpp
    // ---- DrawableSpec default initialization should produce deterministic data ----
    {
        DrawableSpec drawable;
        if (!nearlyEqual (drawable.radius, 0.05))
            return fail ("DrawableSpec default radius should be 0.05.");
        if (!nearlyEqual (drawable.length, 0.1))
            return fail ("DrawableSpec default length should be 0.1.");
        if (!nearlyEqual (drawable.rpyDeg[0], 0.0) ||
            !nearlyEqual (drawable.rpyDeg[1], 0.0) ||
            !nearlyEqual (drawable.rpyDeg[2], 0.0))
            return fail ("DrawableSpec default RPY should be zero.");
        if (!nearlyEqual (drawable.pos[0], 0.0) ||
            !nearlyEqual (drawable.pos[1], 0.0) ||
            !nearlyEqual (drawable.pos[2], 0.0))
            return fail ("DrawableSpec default Pos should be zero.");
        if (!nearlyEqual (drawable.rgb[0], 0.6) ||
            !nearlyEqual (drawable.rgb[1], 0.6) ||
            !nearlyEqual (drawable.rgb[2], 0.6))
            return fail ("DrawableSpec default RGB should be 0.6 0.6 0.6.");
        if (drawable.collisionModel)
            return fail ("DrawableSpec default collisionModel should be false.");
    }
```

- [ ] **Step 2: Run test to verify it fails or is unstable**

Run:

```powershell
cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
```

Expected: may fail with one of the new default initialization messages, or may pass by accident depending on stack contents. Treat any pass before implementation as insufficient; the code still reads uninitialized fields.

- [ ] **Step 3: Add default member initializers**

In `RobotModelSpec.hpp`, replace the `DrawableSpec` definition with:

```cpp
struct DrawableSpec
{
    std::string name;
    std::string refFrame;
    std::string shape = "Box";
    std::string filePath;
    std::array< double, 3 > dimensions = {{0.1, 0.1, 0.1}};
    double radius = 0.05;
    double length = 0.1;
    std::array< double, 3 > rpyDeg = {{0, 0, 0}};
    std::array< double, 3 > pos = {{0, 0, 0}};
    std::array< double, 3 > rgb = {{0.6, 0.6, 0.6}};
    bool collisionModel = false;
    bool autoLinkGeometry = false;
};
```

- [ ] **Step 4: Run regression test**

Run the build and executable command from Step 2.

Expected: executable exits `0`.

- [ ] **Step 5: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "fix: initialize drawable model defaults"
```

---

### Task 3: Reject Scene Frame Forward References And Cycles

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add failing scene dependency tests**

Insert this block near existing validation tests for scene frames:

```cpp
    // ---- Scene frame references must be topologically valid ----
    {
        RobotModelSpec forward = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        forward.sceneFrames.clear ();
        FrameSpec child;
        child.name = "ChildBeforeParent";
        child.refFrame = "LaterParent";
        child.frameType = SceneFrameType::Fixed;
        forward.sceneFrames.push_back (child);
        FrameSpec parent;
        parent.name = "LaterParent";
        parent.refFrame = "WORLD";
        parent.frameType = SceneFrameType::Fixed;
        forward.sceneFrames.push_back (parent);

        QStringList sceneErrors;
        if (RobotModelXmlWriter::validate (forward, sceneErrors))
            return fail ("Scene frame forward references should be rejected.");

        RobotModelSpec cycle = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        cycle.sceneFrames.clear ();
        FrameSpec a;
        a.name = "SceneA";
        a.refFrame = "SceneB";
        a.frameType = SceneFrameType::Fixed;
        cycle.sceneFrames.push_back (a);
        FrameSpec b;
        b.name = "SceneB";
        b.refFrame = "SceneA";
        b.frameType = SceneFrameType::Fixed;
        cycle.sceneFrames.push_back (b);

        sceneErrors.clear ();
        if (RobotModelXmlWriter::validate (cycle, sceneErrors))
            return fail ("Scene frame cycles should be rejected.");
    }
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
```

Expected: executable exits `1` with `Scene frame forward references should be rejected.`

- [ ] **Step 3: Enforce ordered references**

In `RobotModelXmlWriter.cpp`, replace the scene frame reference validation block that builds `availableSceneRefs` from all frames before validating each row. Use this order-sensitive version:

```cpp
        std::set< std::string > declaredSceneRefs;
        declaredSceneRefs.insert ("WORLD");
        declaredSceneRefs.insert ("RobotBase");
        for (const FrameSpec& frame : spec.sceneFrames) {
            if (isEmpty (frame.refFrame)) {
                errors << QString ("Scene frame %1 requires a refframe.")
                              .arg (QString::fromStdString (frame.name));
            }
            else if (declaredSceneRefs.find (frame.refFrame) == declaredSceneRefs.end ()) {
                errors << QString ("Scene frame %1 references frame %2 before it is declared.")
                              .arg (QString::fromStdString (frame.name),
                                    QString::fromStdString (frame.refFrame));
            }
            if (frame.name == frame.refFrame)
                errors << QString ("Scene frame %1 must not reference itself.")
                              .arg (QString::fromStdString (frame.name));
            if (!isEmpty (frame.name))
                declaredSceneRefs.insert (frame.name);
            for (double v : frame.rpyDeg) {
                if (!std::isfinite (v))
                    errors << QString ("Scene frame %1 RPY values must be finite.")
                                  .arg (QString::fromStdString (frame.name));
            }
            for (double v : frame.pos) {
                if (!std::isfinite (v))
                    errors << QString ("Scene frame %1 Pos values must be finite.")
                                  .arg (QString::fromStdString (frame.name));
            }
            for (double v : frame.transform) {
                if (!std::isfinite (v))
                    errors << QString ("Scene frame %1 Transform values must be finite.")
                                  .arg (QString::fromStdString (frame.name));
            }
        }
```

This rejects both forward references and cycles without needing a full graph traversal, because XML is emitted in `spec.sceneFrames` order.

- [ ] **Step 4: Run regression test**

Run the build and executable command from Step 2.

Expected: executable exits `0`.

- [ ] **Step 5: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "fix: validate scene frame dependency order"
```

---

### Task 4: Make URDF Import Preview-Only

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Modify: `docs/RobotModelBuilder.md`

- [ ] **Step 1: Change import workflow**

In `RobotModelBuilderWidget::importUrdf()`, replace the block after `fillFromSpec (result.spec);` with:

```cpp
    fillFromSpec (result.spec);
    generatePreview ();

    if (!result.warnings.isEmpty ()) {
        QMessageBox::information (
            this,
            "URDF Import Warnings",
            result.warnings.join ("\n"));
    }
    setStatus ("URDF imported. Review the preview, then use Save XML or Save and Load.");
```

Remove this block from the same function:

```cpp
    QStringList saveErrors;
    if (!RobotModelXmlWriter::saveFiles (result.spec, saveErrors)) {
        showErrors (saveErrors);
        return;
    }
    if (result.spec.generateScene)
        Q_EMIT loadSceneRequested (RobotModelXmlWriter::sceneFilePath (result.spec));
    else
        Q_EMIT loadSceneRequested (RobotModelXmlWriter::serialDeviceFilePath (result.spec));
```

- [ ] **Step 2: Build plugin target**

Run:

```powershell
cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder --config Release
```

Expected: build exits `0`.

- [ ] **Step 3: Update user/developer docs**

In `docs/RobotModelBuilder.md`, update the button behavior table around `"Generate Preview"`, `"Save XML"`, and `"Save and Load"` so it includes `"Import URDF"`:

```markdown
| 按钮 | 写盘 | 加载场景 | 失败时表现 |
| --- | --- | --- | --- |
| **Import URDF** | ❌ | ❌ | 解析失败弹窗;成功后只填充 UI 并刷新预览 |
| **Generate Preview** | ❌ | ❌ | 仅刷新三段 XML 预览,弹窗报错 |
| **Save XML** | ✅ | ❌ | 失败弹窗,状态栏报错 |
| **Save and Load** | ✅ | ✅(自动加载场景) | 失败弹窗,不触发加载 |
```

Also add this sentence near the URDF import section:

```markdown
URDF 导入不会自动覆盖磁盘文件,也不会自动切换当前 WorkCell;导入成功后用户需要显式点击 **Save XML** 或 **Save and Load**。
```

- [ ] **Step 4: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp docs/RobotModelBuilder.md
git commit -m "fix: make URDF import preview-only"
```

---

### Task 5: Register RobotModelBuilder Test With CTest

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`

- [ ] **Step 1: Add CTest registration**

After `target_include_directories(sdurws_robotmodelbuilder_xmltest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})`, add:

```cmake
    if(BUILD_TESTING)
        add_test(
            NAME sdurws_robotmodelbuilder_xmltest
            COMMAND $<TARGET_FILE:sdurws_robotmodelbuilder_xmltest>
        )
    endif()
```

- [ ] **Step 2: Reconfigure if needed**

If CTest does not see the test after building, run the project’s existing CMake configure command for this build directory. If the exact configure command is not available, open the build directory’s `CMakeCache.txt` and reuse the same generator/source paths from the current build setup.

- [ ] **Step 3: Build and run CTest**

Run:

```powershell
cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release
ctest --test-dir "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" -C Release -R sdurws_robotmodelbuilder_xmltest --output-on-failure
```

Expected: CTest lists `sdurws_robotmodelbuilder_xmltest` and reports it passing.

- [ ] **Step 4: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt
git commit -m "test: register robot model builder xml test"
```

---

### Task 6: Remove Local Code Noise

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`

- [ ] **Step 1: Remove duplicate include**

In `RobotModelBuilderWidget.cpp`, remove the duplicate second line:

```cpp
#include <QCheckBox>
```

Keep exactly one `#include <QCheckBox>`.

- [ ] **Step 2: Remove duplicate `_mode` allocation**

In `RobotModelBuilderWidget::buildUi()`, replace:

```cpp
    _mode = new QComboBox ();
    _mode = new QComboBox ();
```

with:

```cpp
    _mode = new QComboBox ();
```

- [ ] **Step 3: Remove stale disabled implementation**

In `RobotModelXmlWriter.cpp`, delete the whole `#if 0` block that starts before the old `makeSerialDeviceXml` implementation and ends at `#endif` immediately before the active `QString RobotModelXmlWriter::makeSerialDeviceXml (const RobotModelSpec& spec)` definition.

The active function beginning with this signature must remain:

```cpp
QString RobotModelXmlWriter::makeSerialDeviceXml (const RobotModelSpec& spec)
```

- [ ] **Step 4: Build and run regression test**

Run:

```powershell
cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
```

Expected: executable exits `0`.

- [ ] **Step 5: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp
git commit -m "chore: remove robot model builder dead code"
```

---

### Task 7: Refresh Documentation For Current Behavior

**Files:**
- Modify: `docs/RobotModelBuilder.md`
- Modify: `docs/RobotModelBuilderDynamics.md` if it mentions old fixed-joint or auto-import behavior

- [ ] **Step 1: Replace fixed six-joint wording**

In `docs/RobotModelBuilder.md`, replace statements that say `transformJoints[]` is always 6 items with:

```markdown
| `transformJoints[]` | — | 可变项 | **唯一真值**;每行对应一个 Revolute/Prismatic/FixedFrame/ToolFrame。默认模型提供 6 个 Revolute 关节,但 UI 支持增删和排序。 |
```

- [ ] **Step 2: Document CTest coverage**

In the testing section, add:

```markdown
`sdurws_robotmodelbuilder_xmltest` 已注册为 CTest 用例。常用验证命令:

```powershell
cmake --build . --target sdurws_robotmodelbuilder_xmltest --config Release
ctest -C Release -R sdurws_robotmodelbuilder_xmltest --output-on-failure
```
```

- [ ] **Step 3: Document XML escaping constraint**

Add this to the XML writer design notes:

```markdown
所有来自用户输入或 URDF 的字符串在写入 XML 属性或文本节点前必须转义。新增字符串输出点应复用 `RobotModelXmlWriter.cpp` 内部的 XML escaping helper,不要直接把 `std::string` 或 `QString` 拼进 XML。
```

- [ ] **Step 4: Commit**

```powershell
git add docs/RobotModelBuilder.md docs/RobotModelBuilderDynamics.md
git commit -m "docs: update robot model builder hardening notes"
```

---

## Final Verification

- [ ] **Step 1: Build plugin and test executable**

```powershell
cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release
cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder --config Release
```

Expected: both commands exit `0`.

- [ ] **Step 2: Run executable directly**

```powershell
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
```

Expected: exit code `0`.

- [ ] **Step 3: Run via CTest**

```powershell
ctest --test-dir "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" -C Release -R sdurws_robotmodelbuilder_xmltest --output-on-failure
```

Expected: `sdurws_robotmodelbuilder_xmltest` passes.

- [ ] **Step 4: Review generated XML manually for escaping**

Open the temp dump path printed by the test executable and inspect the generated XML files. Confirm special characters appear as XML entities such as `&amp;` and `&quot;` when present in test data.

---

## Suggested Parallelization

These tasks can be delegated independently with low conflict risk:

- Agent A: Task 1 XML escaping.
- Agent B: Task 2 default initialization.
- Agent C: Task 3 scene frame dependency validation.
- Agent D: Task 4 URDF import workflow and docs.
- Agent E: Task 5 CTest registration.
- Agent F: Task 6 cleanup after Tasks 1 and 4 have landed.
- Agent G: Task 7 docs after Tasks 1, 4, and 5 have landed.

Integration order:

1. Land Tasks 1, 2, 3, and 5 first because they add regression coverage.
2. Land Task 4 after confirming the UX change is accepted.
3. Land Task 6 after code-heavy tasks to avoid patch conflicts around deleted blocks.
4. Land Task 7 last so docs match final behavior.

