# RobotModelBuilder Independent CollisionModel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 RobotModelBuilder 增加独立 `CollisionModelSpec`、Collision Models 编辑页和 `<CollisionModel>` XML 输出，使视觉模型和碰撞模型可以分离建模。

**Architecture:** 新增 `CollisionModelSpec`，字段与 `DrawableSpec` 的几何/位姿部分对齐，但不包含颜色和 `colmodel`，并挂到 `RobotModelSpec::collisionModels`。Writer 复用 Milestone 4 的 shape/path/validation 思路，但 CollisionModel 只支持 `Box/Cylinder/Sphere/Cone/Mesh/Polytope`，不支持 `Plane/STL`；旧 `DrawableSpec::collisionModel` 继续保留兼容，新的独立碰撞模型通过单独 `<CollisionModel>` 输出。UI 新增 `Collision Models` tab，支持从当前 Drawable 一键生成简化碰撞模型，之后可手动编辑。

**Tech Stack:** C++17、Qt Widgets、QString/QTextStream/QDir、现有 `sdurws_robotmodelbuilder_xmltest` 命令行测试、RobWorkStudio `robotmodelbuilder` 插件。

---

## Current Context

- 当前源码已经有 Milestone 4 的多几何 `DrawableSpec`：

```cpp
struct DrawableSpec
{
    std::string name;
    std::string refFrame;
    std::string shape;
    std::string filePath;
    std::array< double, 3 > dimensions = {{0.1, 0.1, 0.1}};
    double radius;
    double length;
    std::array< double, 3 > rpyDeg;
    std::array< double, 3 > pos;
    std::array< double, 3 > rgb;
    bool collisionModel;
    bool autoLinkGeometry = false;
};
```

- 当前 `GeometryKind` 已支持 `Box/Cylinder/Sphere/Cone/Plane/STL/Mesh/Polytope/Unknown`。
- 当前 `makeSerialDeviceXml()` 在输出 drawables 后继续输出 limits/poses。Milestone 5 应在 drawables 之后、limits 之前输出独立 `<CollisionModel>`。
- 当前 UI 已有 `setCombo()`、`setShapeCombo()`、`drawableColumnEditableForShape()` 等 helper，可复用到 Collision Models 表。
- 当前 `DrawableSpec::collisionModel` 通过 `<Drawable colmodel="Enabled">` 表达简单碰撞模型；Milestone 5 不删除这个兼容字段，但新增的独立模型不依赖它。
- 当前工作区可能已有未提交的 UI 修复和计划文件。实施时不要回滚用户或其他智能体的修改。

## Files

- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

## Task 1: Add Failing Writer Tests

**Files:**
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add independent CollisionModel XML output test**

Add this block after the Milestone 4 drawable tests and before the final dump section:

```cpp
    // =====================================================================
    //  Milestone 5: Independent CollisionModel
    // =====================================================================
    {
        RobotModelSpec collisionOnly =
            RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        collisionOnly.generateDrawables = false;
        collisionOnly.drawables.clear ();
        collisionOnly.collisionModels.clear ();

        CollisionModelSpec box;
        box.name = "Joint1SimpleCollision";
        box.refFrame = "Joint1";
        box.shape = "Box";
        box.dimensions = {{0.2, 0.1, 0.08}};
        box.radius = 0.05;
        box.length = 0.1;
        box.rpyDeg = {{0, 0, 0}};
        box.pos = {{0, 0, 0.04}};
        collisionOnly.collisionModels.push_back (box);

        QStringList collisionErrors;
        if (!RobotModelXmlWriter::validate (collisionOnly, collisionErrors))
            return fail ("Independent CollisionModel should validate without Drawable: " +
                         collisionErrors.join ("; "));

        const QString xml = RobotModelXmlWriter::makeSerialDeviceXml (collisionOnly);
        if (contains (xml, "<Drawable name=\""))
            return fail ("Disabled drawables should not be required for CollisionModel output.");
        if (!contains (xml, "<CollisionModel name=\"Joint1SimpleCollision\" refframe=\"Joint1\">"))
            return fail ("CollisionModel XML should contain independent model root.");
        if (!contains (xml, "<Box x=\"0.2\" y=\"0.1\" z=\"0.08\" />"))
            return fail ("CollisionModel Box should emit x/y/z dimensions.");
    }
```

- [ ] **Step 2: Add visual STL plus simplified Box collision test**

Add this block after the previous block:

```cpp
    {
        RobotModelSpec split =
            RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        split.drawables.clear ();
        split.collisionModels.clear ();

        DrawableSpec visual;
        visual.name = "Joint1VisualStl";
        visual.refFrame = "Joint1";
        visual.shape = "STL";
        visual.filePath = "meshes/joint1_visual.stl";
        visual.dimensions = {{0.1, 0.1, 0.1}};
        visual.radius = 0.05;
        visual.length = 0.1;
        visual.rpyDeg = {{0, 0, 0}};
        visual.pos = {{0, 0, 0}};
        visual.rgb = {{0.7, 0.7, 0.75}};
        visual.collisionModel = false;
        split.drawables.push_back (visual);

        CollisionModelSpec collision;
        collision.name = "Joint1SimpleBox";
        collision.refFrame = "Joint1";
        collision.shape = "Box";
        collision.dimensions = {{0.18, 0.12, 0.1}};
        collision.radius = 0.05;
        collision.length = 0.1;
        collision.rpyDeg = {{0, 0, 0}};
        collision.pos = {{0, 0, 0}};
        split.collisionModels.push_back (collision);

        QStringList splitErrors;
        if (!RobotModelXmlWriter::validate (split, splitErrors))
            return fail ("Visual STL plus simplified collision should validate: " +
                         splitErrors.join ("; "));
        const QString xml = RobotModelXmlWriter::makeSerialDeviceXml (split);
        if (!contains (xml, "<STL file=\"meshes/joint1_visual.stl\" />"))
            return fail ("Visual STL drawable should still be emitted.");
        if (contains (xml, "<Drawable name=\"Joint1VisualStl\" refframe=\"Joint1\" colmodel=\"Enabled\">"))
            return fail ("Visual-only STL drawable should not be marked colmodel=Enabled.");
        if (!contains (xml, "<CollisionModel name=\"Joint1SimpleBox\" refframe=\"Joint1\">"))
            return fail ("Simplified Box collision should be emitted independently.");
        if (!contains (xml, "<Box x=\"0.18\" y=\"0.12\" z=\"0.1\" />"))
            return fail ("Simplified Box collision dimensions should be emitted.");
    }
```

- [ ] **Step 3: Add validation tests for collision shape/file/refFrame**

Add this block after the previous block:

```cpp
    {
        RobotModelSpec badCollision =
            RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        badCollision.collisionModels.clear ();

        CollisionModelSpec unknownFrame;
        unknownFrame.name = "BadCollisionFrame";
        unknownFrame.refFrame = "MissingJoint";
        unknownFrame.shape = "Box";
        unknownFrame.dimensions = {{0.1, 0.1, 0.1}};
        unknownFrame.radius = 0.05;
        unknownFrame.length = 0.1;
        unknownFrame.rpyDeg = {{0, 0, 0}};
        unknownFrame.pos = {{0, 0, 0}};
        badCollision.collisionModels.push_back (unknownFrame);

        QStringList frameErrors;
        if (RobotModelXmlWriter::validate (badCollision, frameErrors))
            return fail ("CollisionModel referencing unknown frame should fail validation.");
        if (!frameErrors.join (" ").contains ("MissingJoint"))
            return fail ("CollisionModel unknown frame error should mention MissingJoint.");

        RobotModelSpec missingMesh =
            RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        missingMesh.collisionModels.clear ();
        CollisionModelSpec mesh;
        mesh.name = "MissingMeshCollision";
        mesh.refFrame = "Joint1";
        mesh.shape = "Mesh";
        mesh.filePath.clear ();
        mesh.dimensions = {{0.1, 0.1, 0.1}};
        mesh.radius = 0.05;
        mesh.length = 0.1;
        mesh.rpyDeg = {{0, 0, 0}};
        mesh.pos = {{0, 0, 0}};
        missingMesh.collisionModels.push_back (mesh);
        QStringList meshErrors;
        if (RobotModelXmlWriter::validate (missingMesh, meshErrors))
            return fail ("CollisionModel Mesh with empty file path should fail validation.");

        RobotModelSpec unsupported =
            RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        unsupported.collisionModels.clear ();
        CollisionModelSpec plane;
        plane.name = "UnsupportedPlaneCollision";
        plane.refFrame = "Joint1";
        plane.shape = "Plane";
        plane.filePath.clear ();
        plane.dimensions = {{0.1, 0.1, 0.1}};
        plane.radius = 0.05;
        plane.length = 0.1;
        plane.rpyDeg = {{0, 0, 0}};
        plane.pos = {{0, 0, 0}};
        unsupported.collisionModels.push_back (plane);
        QStringList unsupportedErrors;
        if (RobotModelXmlWriter::validate (unsupported, unsupportedErrors))
            return fail ("CollisionModel Plane should be rejected because Milestone 5 supports "
                         "Box/Cylinder/Sphere/Cone/Mesh/Polytope only.");
    }
```

- [ ] **Step 4: Verify tests fail for missing type**

Run:

```powershell
cmd.exe /c "call ""D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake --build ""build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release"" --target sdurws_robotmodelbuilder_xmltest --config Release"
```

Expected: compile fails with `CollisionModelSpec` or `RobotModelSpec::collisionModels` not declared.

- [ ] **Step 5: Commit tests**

After the test compile failure is confirmed, commit only the test changes:

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "新增独立碰撞模型失败测试"
```

## Task 2: Add CollisionModelSpec Data Model

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add `CollisionModelSpec`**

In `RobotModelSpec.hpp`, place this after `DrawableSpec`:

```cpp
struct CollisionModelSpec
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
};
```

Do not add `rgb` or `collisionModel`; `<CollisionModel>` is not a visual object.

- [ ] **Step 2: Add collection to `RobotModelSpec`**

In `RobotModelSpec`, add the collection immediately after `drawables`:

```cpp
    std::vector< CollisionModelSpec > collisionModels;
```

The final model section should read:

```cpp
    std::vector< JointTransformSpec > transformJoints;
    std::vector< DHJointSpec > dhJoints;
    std::vector< DrawableSpec > drawables;
    std::vector< CollisionModelSpec > collisionModels;
    std::vector< JointLimitSpec > limits;
```

- [ ] **Step 3: Initialize default collision models as empty**

No explicit code is required in `makeDefaultSixAxisModel()` if `RobotModelSpec` is value-initialized as it is today. The intended default is:

```cpp
spec.collisionModels.empty () == true
```

This prevents Milestone 5 from changing default robot XML unexpectedly. Users can generate default collision models from the UI in Task 6.

- [ ] **Step 4: Re-run build**

Run:

```powershell
cmd.exe /c "call ""D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake --build ""build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release"" --target sdurws_robotmodelbuilder_xmltest --config Release"
```

Expected: build advances past missing type errors and fails later because writer/validation does not output or validate collision models yet.

- [ ] **Step 5: Commit**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp
git commit -m "新增独立碰撞模型数据结构"
```

## Task 3: Add Writer Helpers and XML Output

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add private helper declarations**

In `RobotModelXmlWriter.hpp`, add these helpers near the existing drawable helpers:

```cpp
    static bool isCollisionModelShapeSupported (GeometryKind kind);
    static QString collisionShapeXml (const RobotModelSpec& spec,
                                      const CollisionModelSpec& collision);
    static void writeCollisionModelXml (QTextStream& out, const RobotModelSpec& spec,
                                        const CollisionModelSpec& collision);
```

- [ ] **Step 2: Implement supported-shape helper**

In `RobotModelXmlWriter.cpp`, after `relativeGeometryPath()`:

```cpp
bool RobotModelXmlWriter::isCollisionModelShapeSupported (GeometryKind kind)
{
    return kind == GeometryKind::Box || kind == GeometryKind::Cylinder ||
           kind == GeometryKind::Sphere || kind == GeometryKind::Cone ||
           kind == GeometryKind::Mesh || kind == GeometryKind::Polytope;
}
```

Milestone 5 intentionally rejects `Plane` and `STL` for `CollisionModelSpec`. Use `Mesh` for file-based collision geometry.

- [ ] **Step 3: Implement collision shape XML helper**

Add:

```cpp
QString RobotModelXmlWriter::collisionShapeXml (const RobotModelSpec& spec,
                                                const CollisionModelSpec& collision)
{
    const GeometryKind kind = geometryKindFromString (collision.shape);
    switch (kind) {
        case GeometryKind::Box:
            return QString ("<Box x=\"%1\" y=\"%2\" z=\"%3\" />")
                .arg (number (collision.dimensions[0]),
                      number (collision.dimensions[1]),
                      number (collision.dimensions[2]));
        case GeometryKind::Cylinder:
            return QString ("<Cylinder radius=\"%1\" z=\"%2\" />")
                .arg (number (collision.radius),
                      number (collision.length));
        case GeometryKind::Sphere:
            return QString ("<Sphere radius=\"%1\" />")
                .arg (number (collision.radius));
        case GeometryKind::Cone:
            return QString ("<Cone radius=\"%1\" z=\"%2\" />")
                .arg (number (collision.radius),
                      number (collision.length));
        case GeometryKind::Mesh:
            return QString ("<Mesh file=\"%1\" />")
                .arg (relativeGeometryPath (spec, collision.filePath));
        case GeometryKind::Polytope:
            return QString ("<Polytope file=\"%1\" />")
                .arg (relativeGeometryPath (spec, collision.filePath));
        case GeometryKind::Plane:
        case GeometryKind::STL:
        case GeometryKind::Unknown:
        default:
            return QString ();
    }
}
```

- [ ] **Step 4: Implement collision model XML writer**

Add:

```cpp
void RobotModelXmlWriter::writeCollisionModelXml (QTextStream& out,
                                                  const RobotModelSpec& spec,
                                                  const CollisionModelSpec& collision)
{
    out << "  <CollisionModel name=\"" << QString::fromStdString (collision.name)
        << "\" refframe=\"" << QString::fromStdString (collision.refFrame) << "\">\n";
    out << "    <RPY>" << vector3 (collision.rpyDeg) << "</RPY>\n";
    out << "    <Pos>" << vector3 (collision.pos) << "</Pos>\n";
    out << "    " << collisionShapeXml (spec, collision) << "\n";
    out << "  </CollisionModel>\n";
}
```

- [ ] **Step 5: Output collision models in both `makeSerialDeviceXml()` implementations**

The file currently has two active `makeSerialDeviceXml()` implementations: one for standard Joint XML and one for `exportDhJointsAdvanced`. In both functions, immediately after the existing drawable output block:

```cpp
    if (spec.generateDrawables) {
        for (const DrawableSpec& drawable : spec.drawables)
            writeDrawableXml (out, spec, drawable);
    }
```

add:

```cpp
    for (const CollisionModelSpec& collision : spec.collisionModels)
        writeCollisionModelXml (out, spec, collision);
```

Do not gate collision models behind `generateDrawables`; the first acceptance test requires independent CollisionModel output when drawables are disabled.

- [ ] **Step 6: Run writer tests**

Run:

```powershell
cmd.exe /c "call ""D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake --build ""build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release"" --target sdurws_robotmodelbuilder_xmltest --config Release"
$env:PATH="D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\bin;D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWork\bin;D:\software\QT\6.11.1\msvc2022_64\bin;" + $env:PATH
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
```

Expected: output tests pass or validation tests still fail until Task 4.

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp
git commit -m "输出独立碰撞模型XML"
```

## Task 4: Validate Collision Models

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add validation block after drawable validation**

In `RobotModelXmlWriter::validate()`, after the `if (spec.generateDrawables)` block, add:

```cpp
    std::set< std::string > collisionNames;
    for (const CollisionModelSpec& collision : spec.collisionModels) {
        if (isEmpty (collision.name))
            errors << "CollisionModel names must not be empty.";
        else if (!collisionNames.insert (collision.name).second)
            errors << QString ("Duplicate CollisionModel name: %1.")
                          .arg (QString::fromStdString (collision.name));

        if (isEmpty (collision.refFrame))
            errors << QString ("CollisionModel %1 requires a reference frame.")
                          .arg (QString::fromStdString (collision.name));
        else if (frameNames.find (collision.refFrame) == frameNames.end ())
            errors << QString ("CollisionModel %1 references unknown frame %2.")
                          .arg (QString::fromStdString (collision.name),
                                QString::fromStdString (collision.refFrame));

        const GeometryKind kind = geometryKindFromString (collision.shape);
        if (!isCollisionModelShapeSupported (kind))
            errors << QString ("CollisionModel %1 has unsupported shape %2.")
                          .arg (QString::fromStdString (collision.name),
                                QString::fromStdString (collision.shape));

        if (kind == GeometryKind::Box &&
            (!(collision.dimensions[0] > 0) || !(collision.dimensions[1] > 0) ||
             !(collision.dimensions[2] > 0))) {
            errors << QString ("CollisionModel %1 Box dimensions must be greater than zero.")
                          .arg (QString::fromStdString (collision.name));
        }
        if ((kind == GeometryKind::Cylinder || kind == GeometryKind::Sphere ||
             kind == GeometryKind::Cone) && !(collision.radius > 0)) {
            errors << QString ("CollisionModel %1 radius must be greater than zero.")
                          .arg (QString::fromStdString (collision.name));
        }
        if ((kind == GeometryKind::Cylinder || kind == GeometryKind::Cone) &&
            !(collision.length > 0)) {
            errors << QString ("CollisionModel %1 length must be greater than zero.")
                          .arg (QString::fromStdString (collision.name));
        }
        if ((kind == GeometryKind::Mesh || kind == GeometryKind::Polytope) &&
            isEmpty (collision.filePath)) {
            errors << QString ("CollisionModel %1 file path is required for %2 geometry.")
                          .arg (QString::fromStdString (collision.name),
                                QString::fromStdString (collision.shape));
        }
        for (double value : collision.dimensions) {
            if (!std::isfinite (value))
                errors << QString ("CollisionModel %1 dimensions must be finite.")
                              .arg (QString::fromStdString (collision.name));
        }
        if (!std::isfinite (collision.radius) || !std::isfinite (collision.length))
            errors << QString ("CollisionModel %1 radius/length must be finite.")
                          .arg (QString::fromStdString (collision.name));
        for (double value : collision.rpyDeg) {
            if (!std::isfinite (value))
                errors << QString ("CollisionModel %1 RPY values must be finite.")
                              .arg (QString::fromStdString (collision.name));
        }
        for (double value : collision.pos) {
            if (!std::isfinite (value))
                errors << QString ("CollisionModel %1 Pos values must be finite.")
                              .arg (QString::fromStdString (collision.name));
        }
    }
```

- [ ] **Step 2: Ensure validation is not tied to `generateDrawables`**

The validation block above must always run, even if `spec.generateDrawables == false`. This preserves the acceptance case:

```cpp
collisionOnly.generateDrawables = false;
collisionOnly.collisionModels.push_back (box);
```

- [ ] **Step 3: Re-run tests**

Run:

```powershell
cmd.exe /c "call ""D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake --build ""build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release"" --target sdurws_robotmodelbuilder_xmltest --config Release"
$env:PATH="D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\bin;D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWork\bin;D:\software\QT\6.11.1\msvc2022_64\bin;" + $env:PATH
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
```

Expected: Milestone 5 validation tests pass.

- [ ] **Step 4: Commit**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "校验独立碰撞模型参数"
```

## Task 5: Add Collision Models UI Tab

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`

- [ ] **Step 1: Add slots and table member**

In `RobotModelBuilderWidget.hpp`, add private slots:

```cpp
    void addCollisionModel ();
    void removeSelectedCollisionModel ();
    void generateCollisionModelsFromDrawables ();
```

Add private methods:

```cpp
    void fillCollisionModelsTable (const RobotModelSpec& spec);
    static bool collisionColumnEditableForShape (const QString& shape, int column);
```

Add member:

```cpp
    QTableWidget* _collisionModelsTable;
```

Place it near `_drawablesTable`.

- [ ] **Step 2: Add tab in `buildUi()`**

Immediately after the Drawables tab, add:

```cpp
    QWidget* collisionTab = new QWidget ();
    QVBoxLayout* collisionLay = new QVBoxLayout (collisionTab);
    _collisionModelsTable = makeTable (
        QStringList () << "Name"
                       << "RefFrame"
                       << "Shape"
                       << "Dimensions x y z"
                       << "Radius"
                       << "Length"
                       << "File"
                       << "RPY deg (Z Y X)"
                       << "Pos m",
        0);
    collisionLay->addWidget (_collisionModelsTable);

    QWidget* collisionButtons = new QWidget ();
    QHBoxLayout* collisionBtnLay = new QHBoxLayout (collisionButtons);
    QPushButton* addCollisionBtn = new QPushButton ("Add Collision Model");
    QPushButton* delCollisionBtn = new QPushButton ("Remove Collision Model");
    QPushButton* genCollisionBtn = new QPushButton ("Generate From Drawables");
    collisionBtnLay->setContentsMargins (0, 0, 0, 0);
    collisionBtnLay->addWidget (addCollisionBtn);
    collisionBtnLay->addWidget (delCollisionBtn);
    collisionBtnLay->addWidget (genCollisionBtn);
    collisionBtnLay->addStretch ();
    collisionLay->addWidget (collisionButtons);
    tabs->addTab (collisionTab, "Collision Models");
```

Add signal connections near the existing button connections:

```cpp
    connect (addCollisionBtn, SIGNAL (clicked ()), this, SLOT (addCollisionModel ()));
    connect (delCollisionBtn, SIGNAL (clicked ()), this, SLOT (removeSelectedCollisionModel ()));
    connect (genCollisionBtn, SIGNAL (clicked ()), this, SLOT (generateCollisionModelsFromDrawables ()));
```

- [ ] **Step 3: Collect collision models**

In `collectSpec()`, after collecting `spec.drawables`, add:

```cpp
    // ---- Collision Models 表(Milestone 5:独立碰撞几何)----
    for (int row = 0; row < _collisionModelsTable->rowCount (); ++row) {
        CollisionModelSpec collision;
        collision.name      = itemText (_collisionModelsTable, row, 0).toStdString ();
        collision.refFrame  = itemText (_collisionModelsTable, row, 1).toStdString ();
        collision.shape     = itemText (_collisionModelsTable, row, 2).toStdString ();
        parseVector3 (itemText (_collisionModelsTable, row, 3), collision.dimensions);
        collision.radius    = itemDouble (_collisionModelsTable, row, 4);
        collision.length    = itemDouble (_collisionModelsTable, row, 5);
        collision.filePath  = itemText (_collisionModelsTable, row, 6).toStdString ();
        parseVector3 (itemText (_collisionModelsTable, row, 7), collision.rpyDeg);
        parseVector3 (itemText (_collisionModelsTable, row, 8), collision.pos);
        spec.collisionModels.push_back (collision);
    }
```

- [ ] **Step 4: Fill collision table**

Implement:

```cpp
void RobotModelBuilderWidget::fillCollisionModelsTable (const RobotModelSpec& spec)
{
    _collisionModelsTable->setRowCount (static_cast< int > (spec.collisionModels.size ()));
    for (int row = 0; row < _collisionModelsTable->rowCount (); ++row) {
        const CollisionModelSpec& collision = spec.collisionModels[row];
        const QString shape = QString::fromStdString (collision.shape);
        setItem (_collisionModelsTable, row, 0, QString::fromStdString (collision.name));
        setItem (_collisionModelsTable, row, 1, QString::fromStdString (collision.refFrame));
        setCombo (_collisionModelsTable, row, 2,
                  QStringList () << "Box" << "Cylinder" << "Sphere" << "Cone"
                                 << "Mesh" << "Polytope",
                  shape);
        setItem (_collisionModelsTable, row, 3, vectorText (collision.dimensions),
                 collisionColumnEditableForShape (shape, 3));
        setItem (_collisionModelsTable, row, 4, QString::number (collision.radius),
                 collisionColumnEditableForShape (shape, 4));
        setItem (_collisionModelsTable, row, 5, QString::number (collision.length),
                 collisionColumnEditableForShape (shape, 5));
        setItem (_collisionModelsTable, row, 6, QString::fromStdString (collision.filePath),
                 collisionColumnEditableForShape (shape, 6));
        setItem (_collisionModelsTable, row, 7, vectorText (collision.rpyDeg));
        setItem (_collisionModelsTable, row, 8, vectorText (collision.pos));
    }
}
```

Call it from `fillFromSpec()` immediately after `fillDrawablesTable(spec)`:

```cpp
    fillCollisionModelsTable (spec);
```

- [ ] **Step 5: Add shape-specific editable columns**

Add:

```cpp
bool RobotModelBuilderWidget::collisionColumnEditableForShape (const QString& shape, int column)
{
    const GeometryKind kind = geometryKindFromString (shape.toStdString ());
    if (column == 3)
        return kind == GeometryKind::Box;
    if (column == 4)
        return kind == GeometryKind::Cylinder || kind == GeometryKind::Sphere ||
               kind == GeometryKind::Cone;
    if (column == 5)
        return kind == GeometryKind::Cylinder || kind == GeometryKind::Cone;
    if (column == 6)
        return kind == GeometryKind::Mesh || kind == GeometryKind::Polytope;
    return true;
}
```

- [ ] **Step 6: Add table input checks**

In `validateTableInput()`, after the drawables table checks, add:

```cpp
    for (int row = 0; row < _collisionModelsTable->rowCount (); ++row) {
        if (!parseVector (itemText (_collisionModelsTable, row, 3), 3))
            errors << QString ("Invalid collision model dimensions vector at row %1.")
                          .arg (row + 1);
        if (!parseDouble (itemText (_collisionModelsTable, row, 4)))
            errors << QString ("Invalid collision model radius at row %1.").arg (row + 1);
        if (!parseDouble (itemText (_collisionModelsTable, row, 5)))
            errors << QString ("Invalid collision model length at row %1.").arg (row + 1);
        if (!parseVector (itemText (_collisionModelsTable, row, 7), 3))
            errors << QString ("Invalid collision model RPY vector at row %1.").arg (row + 1);
        if (!parseVector (itemText (_collisionModelsTable, row, 8), 3))
            errors << QString ("Invalid collision model Pos vector at row %1.").arg (row + 1);
    }
```

This block is not gated by `_generateDrawables`; independent collision models remain valid even when drawables are disabled.

- [ ] **Step 7: Build plugin**

Run:

```powershell
cmd.exe /c "call ""D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake --build ""build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release"" --target sdurws_robotmodelbuilder --config Release"
```

Expected: target builds successfully.

- [ ] **Step 8: Commit**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp
git commit -m "新增碰撞模型编辑界面"
```

## Task 6: Add Collision Model Add/Remove and Generate From Drawables

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`

- [ ] **Step 1: Implement add button**

Add:

```cpp
void RobotModelBuilderWidget::addCollisionModel ()
{
    RobotModelSpec spec = collectSpec ();
    CollisionModelSpec collision;
    collision.name = "CollisionModel" + std::to_string (spec.collisionModels.size () + 1);
    collision.refFrame = spec.transformJoints.empty () ? "Base" : spec.transformJoints.front ().name;
    collision.shape = "Box";
    collision.dimensions = {{0.1, 0.1, 0.1}};
    collision.radius = 0.05;
    collision.length = 0.1;
    collision.rpyDeg = {{0, 0, 0}};
    collision.pos = {{0, 0, 0}};
    spec.collisionModels.push_back (collision);
    fillFromSpec (spec);
    generatePreview ();
}
```

- [ ] **Step 2: Implement remove button**

Add:

```cpp
void RobotModelBuilderWidget::removeSelectedCollisionModel ()
{
    RobotModelSpec spec = collectSpec ();
    const int row = _collisionModelsTable->currentRow ();
    if (row < 0 || row >= static_cast< int > (spec.collisionModels.size ()))
        return;
    spec.collisionModels.erase (spec.collisionModels.begin () + row);
    fillFromSpec (spec);
    generatePreview ();
}
```

- [ ] **Step 3: Implement simplified conversion helper in local scope**

In the anonymous namespace near existing helper functions, add:

```cpp
CollisionModelSpec collisionFromDrawable (const DrawableSpec& drawable, int index)
{
    CollisionModelSpec collision;
    collision.name = drawable.name.empty () ?
                     "CollisionModel" + std::to_string (index + 1) :
                     drawable.name + "Collision";
    collision.refFrame = drawable.refFrame;
    const GeometryKind kind = geometryKindFromString (drawable.shape);
    if (kind == GeometryKind::Cylinder || kind == GeometryKind::Sphere ||
        kind == GeometryKind::Cone) {
        collision.shape = drawable.shape;
        collision.radius = drawable.radius;
        collision.length = drawable.length > 0 ? drawable.length : 0.1;
        collision.dimensions = drawable.dimensions;
    }
    else {
        collision.shape = "Box";
        collision.dimensions = drawable.dimensions;
        collision.radius = drawable.radius > 0 ? drawable.radius : 0.05;
        collision.length = drawable.length > 0 ? drawable.length : 0.1;
    }
    collision.filePath.clear ();
    collision.rpyDeg = drawable.rpyDeg;
    collision.pos = drawable.pos;
    return collision;
}
```

Reasoning: visual `STL/Mesh/Polytope/Plane` defaults to simplified `Box` collision. Existing primitive visual shapes keep their primitive collision shape.

- [ ] **Step 4: Implement generate-from-drawables button**

Add:

```cpp
void RobotModelBuilderWidget::generateCollisionModelsFromDrawables ()
{
    RobotModelSpec spec = collectSpec ();
    spec.collisionModels.clear ();
    int index = 0;
    for (const DrawableSpec& drawable : spec.drawables) {
        if (isAutoLinkDrawable (QString::fromStdString (drawable.name)) ||
            !drawable.name.empty ()) {
            spec.collisionModels.push_back (collisionFromDrawable (drawable, index));
            ++index;
        }
    }
    fillFromSpec (spec);
    generatePreview ();
    setStatus (QString ("Generated %1 collision models from drawables.")
                   .arg (static_cast< int > (spec.collisionModels.size ())));
}
```

This intentionally replaces current collision models; the button label says "Generate From Drawables", and manual editing happens after generation.

- [ ] **Step 5: Synchronize collision models when joints are removed**

In the existing `synchronizeJointDerivedData(...)` helper, after the `spec.drawables.erase(...)` block, add:

```cpp
    spec.collisionModels.erase (
        std::remove_if (spec.collisionModels.begin (), spec.collisionModels.end (),
                        [&] (const CollisionModelSpec& c) {
                            return removedNames.find (c.refFrame) != removedNames.end ();
                        }),
        spec.collisionModels.end ());
```

This matches the user's existing requirement: when a joint/frame is removed, dependent data must not remain with dangling references.

- [ ] **Step 6: Re-run build**

Run:

```powershell
cmd.exe /c "call ""D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake --build ""build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release"" --target sdurws_robotmodelbuilder --config Release"
```

Expected: target builds successfully.

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp
git commit -m "支持从视觉模型生成碰撞模型"
```

## Task 7: Keep Collision Models Synchronized in Joint Editing

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add writer-level regression test for removed frame validation**

Writer tests cannot directly call Widget joint removal, but they can ensure dangling collision references fail. Add this test near the Milestone 5 validation block:

```cpp
    {
        RobotModelSpec dangling =
            RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        dangling.transformJoints.erase (dangling.transformJoints.begin ());
        dangling.collisionModels.clear ();
        CollisionModelSpec collision;
        collision.name = "DanglingCollision";
        collision.refFrame = "Joint1";
        collision.shape = "Box";
        collision.dimensions = {{0.1, 0.1, 0.1}};
        collision.radius = 0.05;
        collision.length = 0.1;
        collision.rpyDeg = {{0, 0, 0}};
        collision.pos = {{0, 0, 0}};
        dangling.collisionModels.push_back (collision);
        QStringList danglingErrors;
        if (RobotModelXmlWriter::validate (dangling, danglingErrors))
            return fail ("Dangling CollisionModel reference should fail validation.");
    }
```

- [ ] **Step 2: Review all joint edit paths**

Confirm these paths preserve or clean `collisionModels`:

- `addJoint()`: no automatic collision model is required; users generate or add manually.
- `removeSelectedJoint()`: calls `synchronizeJointDerivedData(...)`, which now erases collision models whose `refFrame` was removed.
- `moveSelectedJointUp()` / `moveSelectedJointDown()`: no deletion; collision models stay attached by `refFrame` name.
- `synchronizeJointDerivedData(...)`: does not call `applyDefaultDrawables()` in a way that touches `collisionModels`.

- [ ] **Step 3: Run tests and plugin build**

Run:

```powershell
cmd.exe /c "call ""D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake --build ""build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release"" --target sdurws_robotmodelbuilder_xmltest --config Release && cmake --build ""build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release"" --target sdurws_robotmodelbuilder --config Release"
$env:PATH="D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\bin;D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWork\bin;D:\software\QT\6.11.1\msvc2022_64\bin;" + $env:PATH
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
```

Expected: build succeeds and test executable exits 0.

- [ ] **Step 4: Commit**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "同步清理碰撞模型引用"
```

## Task 8: Manual Acceptance and Final Verification

**Files:**
- No source edits expected unless verification finds a bug.

- [ ] **Step 1: Run full Milestone 5 verification**

Run:

```powershell
cmd.exe /c "call ""D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake --build ""build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release"" --target sdurws_robotmodelbuilder_xmltest --config Release && cmake --build ""build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release"" --target sdurws_robotmodelbuilder --config Release"
$env:PATH="D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\bin;D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWork\bin;D:\software\QT\6.11.1\msvc2022_64\bin;" + $env:PATH
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
```

Expected:

- `sdurws_robotmodelbuilder_xmltest` builds.
- `sdurws_robotmodelbuilder` builds.
- Test executable exits 0.

- [ ] **Step 2: Manual UI acceptance**

Open RobotModelBuilder and check:

1. A new `Collision Models` tab exists between `Drawables` and `Scene Frames`.
2. `Add Collision Model` adds a default Box collision model on the first joint.
3. Shape is a dropdown with `Box/Cylinder/Sphere/Cone/Mesh/Polytope`.
4. Shape-specific columns are editable:
   - `Box`: dimensions editable.
   - `Cylinder/Cone`: radius and length editable.
   - `Sphere`: radius editable.
   - `Mesh/Polytope`: file editable.
5. `Generate From Drawables` creates collision models from current drawables.
6. A visual STL drawable plus Box collision model generates XML containing both:

```xml
<Drawable name="..." refframe="...">
  ...
  <STL file="..." />
</Drawable>
<CollisionModel name="..." refframe="...">
  ...
  <Box x="..." y="..." z="..." />
</CollisionModel>
```

7. Remove a joint that has a collision model attached; the collision model row should be removed or validation should block save with a clear missing-frame error. The intended implemented behavior is removal.

- [ ] **Step 3: Check diff**

Run:

```powershell
git -c safe.directory=D:/10_Source_Repos/21_robot/RobWork diff --check
git -c safe.directory=D:/10_Source_Repos/21_robot/RobWork status --short
```

Expected:

- `diff --check` prints no whitespace errors.
- `status --short` contains only intended Milestone 5 files and any pre-existing untracked plan docs.

## Commit Message Rule

All commits for this milestone must use Chinese messages:

```bash
git commit -m "新增独立碰撞模型失败测试"
git commit -m "新增独立碰撞模型数据结构"
git commit -m "输出独立碰撞模型XML"
git commit -m "校验独立碰撞模型参数"
git commit -m "新增碰撞模型编辑界面"
git commit -m "支持从视觉模型生成碰撞模型"
git commit -m "同步清理碰撞模型引用"
```

## Self-Review

- Spec coverage:
  - 新增 `CollisionModelSpec`: Task 2.
  - UI 增加 `Collision Models` tab: Task 5.
  - 支持视觉模型和碰撞模型分离: Task 1 and Task 3.
  - Writer 输出 `<CollisionModel>`: Task 3.
  - 支持 `Box/Cylinder/Sphere/Cone/Mesh/Polytope`: Task 3 and Task 4.
  - 从 Drawable 一键生成简化 collision model: Task 6.
  - 独立 CollisionModel 不依赖 Drawable 输出: Task 1 and Task 3.
  - 同一 frame 视觉 STL + 简化 Box collision: Task 1.
  - 未知 frame validate 失败: Task 1 and Task 4.
  - 关节删除时清理相关 collision model 引用: Task 6 and Task 7.
- Placeholder scan:
  - No `TBD`, `TODO`, `implement later`, or vague "add validation" placeholders.
- Type consistency:
  - `CollisionModelSpec::shape` is a `std::string`, matching `DrawableSpec`.
  - `CollisionModelSpec::dimensions/filePath/radius/length/rpyDeg/pos` names are used consistently in tests, writer, validation, and UI.
  - Collision models are not gated by `generateDrawables`, matching independent output acceptance.
