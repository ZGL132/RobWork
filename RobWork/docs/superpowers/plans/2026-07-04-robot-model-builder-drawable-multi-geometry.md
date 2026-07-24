# RobotModelBuilder Drawable Multi-Geometry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 RobotModelBuilder 的机器人本体 `DrawableSpec` 增加多几何类型支持，使 Drawables 表和 XML writer 能正确生成 Box/Cylinder/Sphere/Cone/Plane/STL/Mesh/Polytope，并保留现有自动圆柱连杆逻辑。

**Architecture:** 复用当前已有的 `GeometryKind` 概念，扩展它支持 `STL/Mesh/Polytope/Unknown`，但 `DrawableSpec` 仍保留 `shape` 字符串作为 UI/序列化输入源。`DrawableSpec` 增加 `filePath` 和 `dimensions`，writer 通过统一 helper 将 `DrawableSpec` 转换为几何 XML；场景几何 `SceneGeometrySpec` 继续工作，但共用 shape 解析和文件几何输出规则。

**Tech Stack:** C++、Qt Widgets、QString/QTextStream、QDir 路径处理、现有命令行测试 `sdurws_robotmodelbuilder_xmltest`。

---

## Current Context

- 当前 `DrawableSpec` 已有字段：`name/refFrame/shape/radius/length/rpyDeg/pos/rgb/collisionModel/autoLinkGeometry`。
- 当前 writer 对机器人本体 drawables 固定输出：

```xml
<Cylinder radius="..." z="..." />
```

即使 `DrawableSpec::shape` 不是 `Cylinder`，也不会生效。

- 当前已有 `SceneGeometrySpec` 和 `GeometryKind`，但只覆盖 `Box/Cylinder/Sphere/Cone/Plane/Mesh`，并且主要服务场景几何。
- Milestone 4 的边界是：机器人本体 `spec.drawables` 多几何；场景几何可以顺手复用扩展后的 helper，但不要把机器人 drawable 和 scene geometry 混成同一个表。
- 自动生成的 `Link{i}To{i+1}` drawable 仍然由 `applyLinkGeometry()` 维护，只锁定这些自动 link 行。用户新增 drawable 即使 shape 是 Cylinder，也必须可自由编辑。

## Files

- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

## Task 1: Add Data Model Fields and Shape Canonicalization

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Write failing tests for all drawable shapes**

Add this block before the final dump section in `RobotModelXmlWriterTest.cpp`:

```cpp
    // =====================================================================
    //  Milestone 4: Drawable multi-geometry
    // =====================================================================
    {
        RobotModelSpec multi = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        multi.drawables.clear ();

        auto addBase = [&] (const std::string& name, const std::string& shape) {
            DrawableSpec d;
            d.name = name;
            d.refFrame = "Joint1";
            d.shape = shape;
            d.radius = 0.05;
            d.length = 0.2;
            d.dimensions = {{0.1, 0.2, 0.3}};
            d.filePath = "meshes/" + name + ".stl";
            d.rpyDeg = {{0, 0, 0}};
            d.pos = {{0, 0, 0}};
            d.rgb = {{0.4, 0.5, 0.6}};
            d.collisionModel = true;
            multi.drawables.push_back (d);
        };

        addBase ("BoxDrawable", "Box");
        addBase ("CylinderDrawable", "Cylinder");
        addBase ("SphereDrawable", "Sphere");
        addBase ("ConeDrawable", "Cone");
        addBase ("PlaneDrawable", "Plane");
        addBase ("STLDrawable", "STL");
        addBase ("MeshDrawable", "Mesh");
        addBase ("PolytopeDrawable", "Polytope");

        QStringList multiErrors;
        if (!RobotModelXmlWriter::validate (multi, multiErrors))
            return fail ("Multi-geometry drawable spec should validate: " + multiErrors.join ("; "));

        const QString xml = RobotModelXmlWriter::makeSerialDeviceXml (multi);
        if (!contains (xml, "<Box x=\"0.1\" y=\"0.2\" z=\"0.3\" />"))
            return fail ("Box drawable should emit Box x/y/z.");
        if (!contains (xml, "<Cylinder radius=\"0.05\" z=\"0.2\" />"))
            return fail ("Cylinder drawable should emit radius/z.");
        if (!contains (xml, "<Sphere radius=\"0.05\" />"))
            return fail ("Sphere drawable should emit radius.");
        if (!contains (xml, "<Cone radius=\"0.05\" z=\"0.2\" />"))
            return fail ("Cone drawable should emit radius/z.");
        if (!contains (xml, "<Plane x=\"0.1\" y=\"0.2\" />"))
            return fail ("Plane drawable should emit x/y.");
        if (!contains (xml, "<STL file=\"meshes/STLDrawable.stl\" />"))
            return fail ("STL drawable should emit file path.");
        if (!contains (xml, "<Mesh file=\"meshes/MeshDrawable.stl\" />"))
            return fail ("Mesh drawable should emit file path.");
        if (!contains (xml, "<Polytope file=\"meshes/PolytopeDrawable.stl\" />"))
            return fail ("Polytope drawable should emit file path.");
        if (xml.count ("colmodel=\"Enabled\"") < 8)
            return fail ("Drawable colmodel=Enabled should be preserved for all shapes.");
    }
```

- [ ] **Step 2: Write failing validation tests**

Add this block after the previous one:

```cpp
    {
        RobotModelSpec invalid = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());

        invalid.drawables[0].shape = "Box";
        invalid.drawables[0].dimensions = {{0, 0.1, 0.1}};
        QStringList boxErrors;
        if (RobotModelXmlWriter::validate (invalid, boxErrors))
            return fail ("Box drawable with zero dimension should fail validation.");

        RobotModelSpec missingFile = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        missingFile.drawables[0].shape = "Mesh";
        missingFile.drawables[0].filePath.clear ();
        QStringList fileErrors;
        if (RobotModelXmlWriter::validate (missingFile, fileErrors))
            return fail ("Mesh drawable with empty file path should fail validation.");

        RobotModelSpec unknown = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        unknown.drawables[0].shape = "Capsule";
        QStringList unknownErrors;
        if (RobotModelXmlWriter::validate (unknown, unknownErrors))
            return fail ("Unknown drawable shape should fail validation.");
    }
```

- [ ] **Step 3: Verify tests fail**

Run:

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release'
$env:PATH="D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\bin;D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWork\bin;D:\software\QT\6.11.1\msvc2022_64\bin;" + $env:PATH
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
```

Expected: compile failure for missing `DrawableSpec::dimensions` / `DrawableSpec::filePath`, or runtime failure because writer still emits Cylinder only.

- [ ] **Step 4: Expand `GeometryKind`**

In `RobotModelSpec.hpp`, change:

```cpp
enum class GeometryKind
{
    Box,
    Cylinder,
    Sphere,
    Cone,
    Plane,
    Mesh
};
```

to:

```cpp
enum class GeometryKind
{
    Box,
    Cylinder,
    Sphere,
    Cone,
    Plane,
    STL,
    Mesh,
    Polytope,
    Unknown
};
```

Update `geometryKindFromString()`:

```cpp
inline GeometryKind geometryKindFromString (const std::string& value)
{
    const std::string v = detail::trimmed (value);
    if (detail::iequals (v, "Box"))
        return GeometryKind::Box;
    if (detail::iequals (v, "Cylinder"))
        return GeometryKind::Cylinder;
    if (detail::iequals (v, "Sphere"))
        return GeometryKind::Sphere;
    if (detail::iequals (v, "Cone"))
        return GeometryKind::Cone;
    if (detail::iequals (v, "Plane"))
        return GeometryKind::Plane;
    if (detail::iequals (v, "STL"))
        return GeometryKind::STL;
    if (detail::iequals (v, "Mesh"))
        return GeometryKind::Mesh;
    if (detail::iequals (v, "Polytope"))
        return GeometryKind::Polytope;
    return GeometryKind::Unknown;
}
```

Update `geometryKindToString()`:

```cpp
inline const char* geometryKindToString (GeometryKind kind)
{
    switch (kind) {
        case GeometryKind::Box: return "Box";
        case GeometryKind::Cylinder: return "Cylinder";
        case GeometryKind::Sphere: return "Sphere";
        case GeometryKind::Cone: return "Cone";
        case GeometryKind::Plane: return "Plane";
        case GeometryKind::STL: return "STL";
        case GeometryKind::Mesh: return "Mesh";
        case GeometryKind::Polytope: return "Polytope";
        case GeometryKind::Unknown:
        default: return "Unknown";
    }
}
```

- [ ] **Step 5: Extend `DrawableSpec`**

Change `DrawableSpec` to:

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

Keep `radius/length` for backward compatibility and auto link cylinders.

- [ ] **Step 6: Initialize new fields where drawables are created**

Every `DrawableSpec` construction in `RobotModelXmlWriter.cpp`, `RobotModelBuilderWidget.cpp`, and tests must set:

```cpp
drawable.shape = "Cylinder";
drawable.filePath.clear ();
drawable.dimensions = {{0.1, 0.1, 0.1}};
```

For existing default housings and links, use Cylinder and preserve current `radius/length`.

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "扩展Drawable几何数据模型"
```

## Task 2: Add Writer Helpers for Drawable Geometry XML

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add helper declarations**

In `RobotModelXmlWriter.hpp`, add private helpers:

```cpp
static QString relativeGeometryPath (const RobotModelSpec& spec, const std::string& filePath);
static QString drawableShapeXml (const RobotModelSpec& spec, const DrawableSpec& drawable);
static void writeDrawableXml (QTextStream& out, const RobotModelSpec& spec,
                              const DrawableSpec& drawable);
```

- [ ] **Step 2: Implement relative path helper**

In `RobotModelXmlWriter.cpp`, add:

```cpp
QString RobotModelXmlWriter::relativeGeometryPath (const RobotModelSpec& spec,
                                                   const std::string& filePath)
{
    const QString raw = QString::fromStdString (filePath).trimmed ();
    if (raw.isEmpty ())
        return raw;
    QFileInfo info (raw);
    if (!info.isAbsolute ())
        return QDir::fromNativeSeparators (raw);
    QDir outDir (QString::fromStdString (spec.saveDirectory));
    return QDir::fromNativeSeparators (outDir.relativeFilePath (info.absoluteFilePath ()));
}
```

Add includes if missing:

```cpp
#include <QFileInfo>
```

- [ ] **Step 3: Implement shape XML helper**

```cpp
QString RobotModelXmlWriter::drawableShapeXml (const RobotModelSpec& spec,
                                               const DrawableSpec& drawable)
{
    const GeometryKind kind = geometryKindFromString (drawable.shape);
    switch (kind) {
        case GeometryKind::Box:
            return QString ("<Box x=\"%1\" y=\"%2\" z=\"%3\" />")
                .arg (number (drawable.dimensions[0]), number (drawable.dimensions[1]),
                      number (drawable.dimensions[2]));
        case GeometryKind::Cylinder:
            return QString ("<Cylinder radius=\"%1\" z=\"%2\" />")
                .arg (number (drawable.radius), number (drawable.length));
        case GeometryKind::Sphere:
            return QString ("<Sphere radius=\"%1\" />").arg (number (drawable.radius));
        case GeometryKind::Cone:
            return QString ("<Cone radius=\"%1\" z=\"%2\" />")
                .arg (number (drawable.radius), number (drawable.length));
        case GeometryKind::Plane:
            return QString ("<Plane x=\"%1\" y=\"%2\" />")
                .arg (number (drawable.dimensions[0]), number (drawable.dimensions[1]));
        case GeometryKind::STL:
            return QString ("<STL file=\"%1\" />")
                .arg (relativeGeometryPath (spec, drawable.filePath));
        case GeometryKind::Mesh:
            return QString ("<Mesh file=\"%1\" />")
                .arg (relativeGeometryPath (spec, drawable.filePath));
        case GeometryKind::Polytope:
            return QString ("<Polytope file=\"%1\" />")
                .arg (relativeGeometryPath (spec, drawable.filePath));
        case GeometryKind::Unknown:
        default:
            return QString ();
    }
}
```

- [ ] **Step 4: Implement drawable XML writer**

```cpp
void RobotModelXmlWriter::writeDrawableXml (QTextStream& out, const RobotModelSpec& spec,
                                            const DrawableSpec& drawable)
{
    out << "  <Drawable name=\"" << QString::fromStdString (drawable.name)
        << "\" refframe=\"" << QString::fromStdString (drawable.refFrame) << "\"";
    if (drawable.collisionModel)
        out << " colmodel=\"Enabled\"";
    out << ">\n";
    out << "    <RPY>" << vector3 (drawable.rpyDeg) << "</RPY>\n";
    out << "    <Pos>" << vector3 (drawable.pos) << "</Pos>\n";
    out << "    <RGB>" << vector3 (drawable.rgb) << "</RGB>\n";
    out << "    " << drawableShapeXml (spec, drawable) << "\n";
    out << "  </Drawable>\n";
}
```

- [ ] **Step 5: Replace fixed Cylinder output**

In both active `makeSerialDeviceXml()` implementations if duplicate active blocks exist, replace:

```cpp
out << "  <Drawable name=\"" << QString::fromStdString (drawable.name)
...
out << "    <Cylinder radius=\"" << number (drawable.radius) << "\" z=\""
    << number (drawable.length) << "\" />\n";
out << "  </Drawable>\n";
```

with:

```cpp
writeDrawableXml (out, spec, drawable);
```

If one block is inside `#if 0`, do not spend time updating it unless it is compiled.

- [ ] **Step 6: Run tests**

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release'
$env:PATH="D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\bin;D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWork\bin;D:\software\QT\6.11.1\msvc2022_64\bin;" + $env:PATH
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
```

Expected: multi-shape output assertions pass once validation is updated in Task 3. If validation still rejects non-Cylinder shapes, continue to Task 3.

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp
git commit -m "按Drawable形状输出几何XML"
```

## Task 3: Validate Drawable Shape-Specific Parameters

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Replace old radius/length-only validation**

In `validate()`, inside `if (spec.generateDrawables)`, replace unconditional:

```cpp
if (drawable.radius <= 0)
    errors << ...
if (drawable.length <= 0)
    errors << ...
```

with shape-specific validation:

```cpp
const GeometryKind kind = geometryKindFromString (drawable.shape);
if (kind == GeometryKind::Unknown) {
    errors << QString ("Drawable %1 has unsupported shape %2.")
                  .arg (QString::fromStdString (drawable.name),
                        QString::fromStdString (drawable.shape));
}
if (kind == GeometryKind::Box &&
    (!(drawable.dimensions[0] > 0) || !(drawable.dimensions[1] > 0) ||
     !(drawable.dimensions[2] > 0))) {
    errors << QString ("Drawable %1 Box dimensions must be greater than zero.")
                  .arg (QString::fromStdString (drawable.name));
}
if (kind == GeometryKind::Plane &&
    (!(drawable.dimensions[0] > 0) || !(drawable.dimensions[1] > 0))) {
    errors << QString ("Drawable %1 Plane dimensions must be greater than zero.")
                  .arg (QString::fromStdString (drawable.name));
}
if ((kind == GeometryKind::Cylinder || kind == GeometryKind::Sphere ||
     kind == GeometryKind::Cone) && !(drawable.radius > 0)) {
    errors << QString ("Drawable %1 radius must be greater than zero.")
                  .arg (QString::fromStdString (drawable.name));
}
if ((kind == GeometryKind::Cylinder || kind == GeometryKind::Cone) &&
    !(drawable.length > 0)) {
    errors << QString ("Drawable %1 length must be greater than zero.")
                  .arg (QString::fromStdString (drawable.name));
}
if ((kind == GeometryKind::STL || kind == GeometryKind::Mesh ||
     kind == GeometryKind::Polytope) && isEmpty (drawable.filePath)) {
    errors << QString ("Drawable %1 file path is required for %2 geometry.")
                  .arg (QString::fromStdString (drawable.name),
                        QString::fromStdString (drawable.shape));
}
```

Keep RGB validation unchanged.

- [ ] **Step 2: Validate dimensions are finite**

Add:

```cpp
for (double value : drawable.dimensions) {
    if (!std::isfinite (value))
        errors << QString ("Drawable %1 dimensions must be finite.")
                      .arg (QString::fromStdString (drawable.name));
}
if (!std::isfinite (drawable.radius) || !std::isfinite (drawable.length))
    errors << QString ("Drawable %1 radius/length must be finite.")
                  .arg (QString::fromStdString (drawable.name));
```

- [ ] **Step 3: Ensure disabled Drawables still skip validation**

Existing test near `noDrawables.generateDrawables = false` must continue to pass. If new validation accidentally runs when drawables are disabled, move all drawable shape checks inside the existing `if (spec.generateDrawables)` block.

- [ ] **Step 4: Run tests and commit**

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release'
$env:PATH="D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\bin;D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWork\bin;D:\software\QT\6.11.1\msvc2022_64\bin;" + $env:PATH
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "校验Drawable几何参数"
```

## Task 4: Store File Geometry Paths Relative to Output Directory

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add relative path test**

Add this test block:

```cpp
    {
        RobotModelSpec rel = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        rel.drawables.clear ();
        DrawableSpec mesh;
        mesh.name = "RelativeMesh";
        mesh.refFrame = "Joint1";
        mesh.shape = "STL";
        mesh.filePath = (QDir::tempPath () + "/robotmodelbuilder_meshes/part.stl").toStdString ();
        mesh.dimensions = {{0.1, 0.1, 0.1}};
        mesh.radius = 0.05;
        mesh.length = 0.1;
        mesh.rpyDeg = {{0, 0, 0}};
        mesh.pos = {{0, 0, 0}};
        mesh.rgb = {{0.3, 0.3, 0.3}};
        mesh.collisionModel = false;
        rel.drawables.push_back (mesh);
        const QString xml = RobotModelXmlWriter::makeSerialDeviceXml (rel);
        if (xml.contains (QDir::tempPath ()))
            return fail ("Absolute mesh path should be saved relative to output directory.");
        if (!contains (xml, "<STL file=\"robotmodelbuilder_meshes/part.stl\" />"))
            return fail ("STL path should be relative to saveDirectory.");
    }
```

Because `rel.saveDirectory == QDir::tempPath()`, the absolute path inside temp should become `robotmodelbuilder_meshes/part.stl`.

- [ ] **Step 2: Ensure `relativeGeometryPath()` is used for STL/Mesh/Polytope**

The helper from Task 2 must be called in all file geometry tags. No extra writer path conversion should exist elsewhere.

- [ ] **Step 3: Run tests and commit**

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release'
$env:PATH="D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\bin;D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWork\bin;D:\software\QT\6.11.1\msvc2022_64\bin;" + $env:PATH
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "保存文件几何相对路径"
```

## Task 5: Update Drawables UI Table

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`

- [ ] **Step 1: Change Drawables table headers**

Replace current headers:

```cpp
QStringList () << "Name"
               << "RefFrame"
               << "Shape"
               << "Radius"
               << "Length"
               << "RPY deg (Z Y X)"
               << "Pos m"
               << "RGB"
               << "Collision"
```

with:

```cpp
QStringList () << "Name"
               << "RefFrame"
               << "Shape"
               << "Dimensions x y z"
               << "Radius"
               << "Length"
               << "File"
               << "RPY deg (Z Y X)"
               << "Pos m"
               << "RGB"
               << "Collision"
```

- [ ] **Step 2: Collect new columns**

Replace drawable collection with:

```cpp
DrawableSpec drawable;
drawable.name = itemText (_drawablesTable, row, 0).toStdString ();
drawable.refFrame = itemText (_drawablesTable, row, 1).toStdString ();
drawable.shape = itemText (_drawablesTable, row, 2).toStdString ();
parseVector3 (itemText (_drawablesTable, row, 3), drawable.dimensions);
drawable.radius = itemDouble (_drawablesTable, row, 4);
drawable.length = itemDouble (_drawablesTable, row, 5);
drawable.filePath = itemText (_drawablesTable, row, 6).toStdString ();
parseVector3 (itemText (_drawablesTable, row, 7), drawable.rpyDeg);
parseVector3 (itemText (_drawablesTable, row, 8), drawable.pos);
parseVector3 (itemText (_drawablesTable, row, 9), drawable.rgb);
drawable.collisionModel =
    itemText (_drawablesTable, row, 10).compare ("Enabled", Qt::CaseInsensitive) == 0 ||
    itemText (_drawablesTable, row, 10).compare ("true", Qt::CaseInsensitive) == 0;
drawable.autoLinkGeometry = isAutoLinkDrawable (QString::fromStdString (drawable.name));
spec.drawables.push_back (drawable);
```

- [ ] **Step 3: Fill new columns**

Update `fillDrawablesTable()`:

```cpp
setItem (_drawablesTable, row, 0, QString::fromStdString (drawable.name), !autoLink);
setItem (_drawablesTable, row, 1, QString::fromStdString (drawable.refFrame), !autoLink);
setItem (_drawablesTable, row, 2, QString::fromStdString (drawable.shape), !autoLink);
setItem (_drawablesTable, row, 3, vectorText (drawable.dimensions), !autoLink);
setItem (_drawablesTable, row, 4, QString::number (drawable.radius));
setItem (_drawablesTable, row, 5, QString::number (drawable.length), !autoLink);
setItem (_drawablesTable, row, 6, QString::fromStdString (drawable.filePath), !autoLink);
setItem (_drawablesTable, row, 7, vectorText (drawable.rpyDeg), !autoLink);
setItem (_drawablesTable, row, 8, vectorText (drawable.pos), !autoLink);
setItem (_drawablesTable, row, 9, vectorText (drawable.rgb));
setItem (_drawablesTable, row, 10, drawable.collisionModel ? "Enabled" : "Disabled");
```

Important: only `Link{i}To{i+1}` rows should be locked by `autoLink`. Default housing rows should remain editable except where existing behavior intentionally allows radius/RGB/collision editing.

- [ ] **Step 4: Shape column should be a combo box**

Add helper declarations in `RobotModelBuilderWidget.hpp`:

```cpp
static QComboBox* makeShapeCombo (const QString& currentShape, bool editable);
void setShapeCombo (QTableWidget* table, int row, int column, const QString& value,
                    bool editable = true);
```

Implement:

```cpp
QComboBox* RobotModelBuilderWidget::makeShapeCombo (const QString& currentShape, bool editable)
{
    QComboBox* combo = new QComboBox ();
    combo->addItems (QStringList () << "Box" << "Cylinder" << "Sphere" << "Cone"
                                    << "Plane" << "STL" << "Mesh" << "Polytope");
    const int index = combo->findText (currentShape, Qt::MatchFixedString);
    combo->setCurrentIndex (index >= 0 ? index : combo->findText ("Cylinder"));
    combo->setEnabled (editable);
    return combo;
}

void RobotModelBuilderWidget::setShapeCombo (QTableWidget* table, int row, int column,
                                             const QString& value, bool editable)
{
    table->setCellWidget (row, column, makeShapeCombo (value, editable));
    setItem (table, row, column, value, false);
}
```

Then in `fillDrawablesTable()`, replace shape `setItem` with:

```cpp
setShapeCombo (_drawablesTable, row, 2, QString::fromStdString (drawable.shape), !autoLink);
```

Update `itemText()` so if a cell has a `QComboBox`, it returns `combo->currentText()` before falling back to `QTableWidgetItem`.

```cpp
if (QWidget* widget = table->cellWidget (row, column)) {
    if (QComboBox* combo = qobject_cast< QComboBox* > (widget))
        return combo->currentText ();
}
```

- [ ] **Step 5: Shape-specific editable columns**

Implement:

```cpp
static bool drawableColumnEditableForShape (const QString& shape, int column, bool autoLink)
{
    if (autoLink)
        return column == 4 || column == 9 || column == 10;
    const GeometryKind kind = geometryKindFromString (shape.toStdString ());
    if (column == 3)
        return kind == GeometryKind::Box || kind == GeometryKind::Plane;
    if (column == 4)
        return kind == GeometryKind::Cylinder || kind == GeometryKind::Sphere ||
               kind == GeometryKind::Cone;
    if (column == 5)
        return kind == GeometryKind::Cylinder || kind == GeometryKind::Cone;
    if (column == 6)
        return kind == GeometryKind::STL || kind == GeometryKind::Mesh ||
               kind == GeometryKind::Polytope;
    return true;
}
```

Use this when filling columns 3-6. If dynamic enable/disable on combo change is too much for this milestone, it is acceptable that enable state updates after Generate Preview/fillFromSpec. The combo itself must exist.

- [ ] **Step 6: Update UI validation column indexes**

In `validateTableInput()`, update Drawables checks:

```cpp
if (!parseVector (itemText (_drawablesTable, row, 3), 3))
    errors << QString ("Invalid drawable dimensions vector at row %1.").arg (row + 1);
if (!parseDouble (itemText (_drawablesTable, row, 4)))
    errors << QString ("Invalid drawable radius at row %1.").arg (row + 1);
if (!parseDouble (itemText (_drawablesTable, row, 5)))
    errors << QString ("Invalid drawable length at row %1.").arg (row + 1);
if (!parseVector (itemText (_drawablesTable, row, 7), 3))
    errors << QString ("Invalid drawable RPY vector at row %1.").arg (row + 1);
if (!parseVector (itemText (_drawablesTable, row, 8), 3))
    errors << QString ("Invalid drawable Pos vector at row %1.").arg (row + 1);
if (!parseVector (itemText (_drawablesTable, row, 9), 3))
    errors << QString ("Invalid drawable RGB vector at row %1.").arg (row + 1);
```

- [ ] **Step 7: Build plugin and commit**

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder --config Release'
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp
git commit -m "完善Drawable多几何编辑界面"
```

## Task 6: Preserve Auto Link Logic and User Editable Drawables

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add regression test for auto link shape**

Add:

```cpp
    {
        RobotModelSpec links = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        RobotModelXmlWriter::applyDefaultDrawables (links);
        bool sawLinkCylinder = false;
        bool sawHousingEditableShape = false;
        for (const DrawableSpec& d : links.drawables) {
            if (d.name == "Link1To2" && d.shape == "Cylinder" && d.autoLinkGeometry)
                sawLinkCylinder = true;
            if (d.name == "Joint1Housing" && d.shape == "Cylinder" && !d.autoLinkGeometry)
                sawHousingEditableShape = true;
        }
        if (!sawLinkCylinder)
            return fail ("Auto link drawables should remain Cylinder and autoLinkGeometry=true.");
        if (!sawHousingEditableShape)
            return fail ("Housing drawables should not be marked as auto-link locked.");
    }
```

- [ ] **Step 2: Ensure auto-created links initialize new fields**

In `appendLinks()` or equivalent helper, ensure:

```cpp
drawable.shape = "Cylinder";
drawable.dimensions = {{0.1, 0.1, 0.1}};
drawable.filePath.clear ();
drawable.autoLinkGeometry = true;
```

In housing creation:

```cpp
drawable.shape = "Cylinder";
drawable.dimensions = {{0.1, 0.1, 0.1}};
drawable.filePath.clear ();
drawable.autoLinkGeometry = false;
```

- [ ] **Step 3: Ensure `applyLinkGeometry()` only mutates auto links**

Confirm the existing loop still has:

```cpp
if (!drawable.autoLinkGeometry)
    continue;
```

If missing, add it. Do not mutate user-created Cylinder drawables.

- [ ] **Step 4: Run tests and commit**

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release'
$env:PATH="D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\bin;D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWork\bin;D:\software\QT\6.11.1\msvc2022_64\bin;" + $env:PATH
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "保持自动连杆圆柱逻辑"
```

## Task 7: Final Verification

**Files:**
- No source files expected unless verification reveals a bug.

- [ ] **Step 1: Run XML writer test**

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release'
$env:PATH="D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\bin;D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWork\bin;D:\software\QT\6.11.1\msvc2022_64\bin;" + $env:PATH
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
```

Expected: executable exits 0.

- [ ] **Step 2: Build plugin**

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder --config Release'
```

Expected: target builds successfully. Existing upstream warnings are acceptable if there are no new RobotModelBuilder errors.

- [ ] **Step 3: Manual acceptance**

1. Open RobotModelBuilder.
2. In Drawables tab, verify Shape is a combo box with `Box/Cylinder/Sphere/Cone/Plane/STL/Mesh/Polytope`.
3. Change a non-auto drawable to `Box`, set dimensions, generate preview, verify `<Box x=... y=... z=... />`.
4. Change a non-auto drawable to `STL`, set an absolute path under save directory, generate preview, verify XML path is relative.
5. Verify `Link1To2` still updates from joint geometry and remains locked except radius/RGB/collision columns according to existing behavior.
6. Add invalid `Box` dimensions or empty `Mesh` file and confirm validation blocks save/preview.

## Commit Message Rule

All commits for this milestone must use Chinese messages:

```bash
git commit -m "扩展Drawable几何数据模型"
git commit -m "按Drawable形状输出几何XML"
git commit -m "校验Drawable几何参数"
git commit -m "保存文件几何相对路径"
git commit -m "完善Drawable多几何编辑界面"
git commit -m "保持自动连杆圆柱逻辑"
```

## Self-Review

- Spec coverage:
  - `DrawableSpec` 增加 `filePath/dimensions`：Task 1。
  - 支持 Box/Cylinder/Sphere/Cone/Plane/STL/Mesh/Polytope：Task 1-3。
  - UI shape 下拉框和 shape 参数列：Task 5。
  - 文件几何相对路径：Task 4。
  - 保留自动圆柱连杆、只锁 auto link：Task 5-6。
  - Writer 按 shape 输出 XML：Task 2。
  - 验收测试：Task 1-4、Task 6。
- Placeholder scan:
  - 无 `TBD`、`TODO`、`implement later`。
- Type consistency:
  - `GeometryKind`、`DrawableSpec::dimensions`、`DrawableSpec::filePath` 在所有任务中一致。
