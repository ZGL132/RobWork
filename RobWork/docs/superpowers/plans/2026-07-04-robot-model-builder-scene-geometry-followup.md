# RobotModelBuilder Scene Geometry Follow-up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有 Milestone 3 Scene Frames 基础上补齐场景几何体建模、校验和 UI 同步，使 Table、Workpiece、MovableBox 等场景对象不仅有 Frame，也有可视几何和可选碰撞模型。

**Architecture:** 保持 Frame 与 Geometry 分离：`FrameSpec` 只表示坐标系和父子关系，新增 `SceneGeometrySpec` 表示挂载到场景 frame 的可视/碰撞几何。XML writer 复用一个统一几何输出函数，避免设备 Drawable 和场景 Drawable 两套不同语义；Widget 增加 Scene Geometry 表，并在删除 scene frame 时同步处理依赖它的 scene geometry。

**Tech Stack:** C++、Qt Widgets、QString/QTextStream、现有命令行测试 `sdurws_robotmodelbuilder_xmltest`。

---

## Current Review Findings

1. **场景对象目前只有 Frame，没有场景几何体。**
   - 位置：`RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp:402-435`、`RobotModelXmlWriter.cpp:1015-1029`
   - 影响：`Table`、`Workpiece`、`MovableBox` 会出现在 XML 中作为坐标系，但 RobWorkStudio 中看不到桌子/工件/箱子，也不能通过 `colmodel="Enabled"` 参与碰撞。作为 WorkCell 场景构建器，这会让 Milestone 3 停留在“坐标系编辑器”层面。

2. **`DrawableSpec::shape` 已存在但 writer 固定输出 `<Cylinder>`。**
   - 位置：`RobotModelXmlWriter.cpp:831-840`、`RobotModelXmlWriter.cpp:955-964`
   - 影响：UI 里 Shape 列看似可编辑，但实际 XML 不尊重 `Box/Sphere/Cone/Plane/Mesh` 等类型。后续加场景几何前，必须先收口成统一几何序列化函数，否则场景几何会复制这个问题。

3. **Scene Geometry 引用边界尚不存在。**
   - 当前 `validate()` 只校验设备 drawables 引用设备内部 frames；场景 frame 已单独校验，但没有 scene geometry，因此也没有“几何体引用不存在 frame 时 validate 报错”的能力。

4. **删除 scene frame 只重定向其他 scene frame，未来必须同步处理 scene geometry。**
   - 位置：`RobotModelBuilderWidget.cpp:919-927`
   - 影响：加入场景几何后，如果删除 `Table`，`TableTop`、`WorkpieceBox` 等引用旧 frame 的几何体不能遗留，否则会出现用户提醒的“主体删除了，相关数据还留着”的一致性问题。

5. **Transform 4x4 列在 RPYPos 模式下也被强制校验。**
   - 位置：`RobotModelBuilderWidget.cpp:1198-1206`
   - 影响：不是阻塞缺陷，因为默认会填 identity；但用户只使用 RPY/Pos 时仍要保留 16 个数，交互上容易困惑。后续 UI 可在 `PoseMode=RPYPos` 时不强制校验 Transform，或保留只读 identity。

## Files

- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

## Task 1: Add Scene Geometry Data Model

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Write failing tests for default scene geometry**

Add this block after the existing Milestone 3 scene frame test:

```cpp
    {
        RobotModelSpec sceneGeo = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        const QString scene = RobotModelXmlWriter::makeSceneXml (sceneGeo);
        if (!contains (scene, "<Drawable name=\"TableTop\" refframe=\"Table\" colmodel=\"Enabled\">"))
            return fail ("Scene XML should contain a visible/collision TableTop drawable.");
        if (!contains (scene, "<Box x=\"1.2\" y=\"0.8\" z=\"0.05\" />"))
            return fail ("TableTop should be emitted as a Box.");
        if (!contains (scene, "<Drawable name=\"WorkpieceBox\" refframe=\"Workpiece\" colmodel=\"Enabled\">"))
            return fail ("Scene XML should contain WorkpieceBox geometry.");
        if (!contains (scene, "<Drawable name=\"MovableBoxGeom\" refframe=\"MovableBox\" colmodel=\"Enabled\">"))
            return fail ("Scene XML should contain MovableBox geometry.");
    }
```

- [ ] **Step 2: Add data model**

Add this enum and struct after `FrameSpec`:

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

struct SceneGeometrySpec
{
    std::string name;
    std::string refFrame;
    GeometryKind kind = GeometryKind::Box;
    std::array< double, 3 > size = {{0.1, 0.1, 0.1}};
    double radius = 0.05;
    double length = 0.1;
    std::string file;
    std::array< double, 3 > rpyDeg = {{0, 0, 0}};
    std::array< double, 3 > pos = {{0, 0, 0}};
    std::array< double, 3 > rgb = {{0.6, 0.6, 0.6}};
    bool collisionModel = true;
};
```

Add conversion helpers:

```cpp
inline GeometryKind geometryKindFromString (const std::string& value)
{
    const std::string v = detail::trimmed (value);
    if (detail::iequals (v, "Cylinder"))
        return GeometryKind::Cylinder;
    if (detail::iequals (v, "Sphere"))
        return GeometryKind::Sphere;
    if (detail::iequals (v, "Cone"))
        return GeometryKind::Cone;
    if (detail::iequals (v, "Plane"))
        return GeometryKind::Plane;
    if (detail::iequals (v, "Mesh") || detail::iequals (v, "STL"))
        return GeometryKind::Mesh;
    return GeometryKind::Box;
}

inline const char* geometryKindToString (GeometryKind kind)
{
    switch (kind) {
        case GeometryKind::Cylinder: return "Cylinder";
        case GeometryKind::Sphere: return "Sphere";
        case GeometryKind::Cone: return "Cone";
        case GeometryKind::Plane: return "Plane";
        case GeometryKind::Mesh: return "Mesh";
        case GeometryKind::Box:
        default: return "Box";
    }
}
```

Add this field to `RobotModelSpec` immediately after `sceneFrames`:

```cpp
std::vector< SceneGeometrySpec > sceneGeometries;
```

- [ ] **Step 3: Commit**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "添加场景几何数据模型"
```

## Task 2: Serialize Scene Geometry and Generalize Shape Output

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add writer declarations**

Add private helper declarations:

```cpp
static QString geometryShapeXml (const SceneGeometrySpec& geometry);
static void writeSceneGeometryXml (QTextStream& out, const SceneGeometrySpec& geometry);
```

- [ ] **Step 2: Add default scene geometries**

In `makeDefaultSixAxisModel()`, after adding default `sceneFrames`, add:

```cpp
SceneGeometrySpec tableTop;
tableTop.name = "TableTop";
tableTop.refFrame = "Table";
tableTop.kind = GeometryKind::Box;
tableTop.size = {{1.2, 0.8, 0.05}};
tableTop.pos = {{0, 0, 0}};
tableTop.rgb = {{0.55, 0.55, 0.55}};
tableTop.collisionModel = true;
spec.sceneGeometries.push_back (tableTop);

SceneGeometrySpec workpieceBox;
workpieceBox.name = "WorkpieceBox";
workpieceBox.refFrame = "Workpiece";
workpieceBox.kind = GeometryKind::Box;
workpieceBox.size = {{0.12, 0.08, 0.05}};
workpieceBox.rgb = {{0.2, 0.55, 0.8}};
workpieceBox.collisionModel = true;
spec.sceneGeometries.push_back (workpieceBox);

SceneGeometrySpec movableBoxGeom;
movableBoxGeom.name = "MovableBoxGeom";
movableBoxGeom.refFrame = "MovableBox";
movableBoxGeom.kind = GeometryKind::Box;
movableBoxGeom.size = {{0.08, 0.08, 0.08}};
movableBoxGeom.rgb = {{0.8, 0.35, 0.2}};
movableBoxGeom.collisionModel = true;
spec.sceneGeometries.push_back (movableBoxGeom);
```

- [ ] **Step 3: Implement geometry XML helpers**

```cpp
QString RobotModelXmlWriter::geometryShapeXml (const SceneGeometrySpec& geometry)
{
    switch (geometry.kind) {
        case GeometryKind::Cylinder:
            return QString ("<Cylinder radius=\"%1\" z=\"%2\" />")
                .arg (number (geometry.radius), number (geometry.length));
        case GeometryKind::Sphere:
            return QString ("<Sphere radius=\"%1\" />").arg (number (geometry.radius));
        case GeometryKind::Cone:
            return QString ("<Cone radius=\"%1\" z=\"%2\" />")
                .arg (number (geometry.radius), number (geometry.length));
        case GeometryKind::Plane:
            return QString ("<Plane x=\"%1\" y=\"%2\" />")
                .arg (number (geometry.size[0]), number (geometry.size[1]));
        case GeometryKind::Mesh:
            return QString ("<Mesh file=\"%1\" />").arg (QString::fromStdString (geometry.file));
        case GeometryKind::Box:
        default:
            return QString ("<Box x=\"%1\" y=\"%2\" z=\"%3\" />")
                .arg (number (geometry.size[0]), number (geometry.size[1]),
                      number (geometry.size[2]));
    }
}

void RobotModelXmlWriter::writeSceneGeometryXml (QTextStream& out,
                                                 const SceneGeometrySpec& geometry)
{
    out << "  <Drawable name=\"" << QString::fromStdString (geometry.name)
        << "\" refframe=\"" << QString::fromStdString (geometry.refFrame) << "\"";
    if (geometry.collisionModel)
        out << " colmodel=\"Enabled\"";
    out << ">\n";
    out << "    <RPY>" << vector3 (geometry.rpyDeg) << "</RPY>\n";
    out << "    <Pos>" << vector3 (geometry.pos) << "</Pos>\n";
    out << "    <RGB>" << vector3 (geometry.rgb) << "</RGB>\n";
    out << "    " << geometryShapeXml (geometry) << "\n";
    out << "  </Drawable>\n";
}
```

- [ ] **Step 4: Update `makeSceneXml()`**

After writing `sceneFrames`, before `<Include>`, add:

```cpp
    for (const SceneGeometrySpec& geometry : spec.sceneGeometries)
        writeSceneGeometryXml (out, geometry);
    if (!spec.sceneGeometries.empty ())
        out << "\n";
```

- [ ] **Step 5: Run test**

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release'
$env:PATH="D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\bin;D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWork\bin;D:\software\QT\6.11.1\msvc2022_64\bin;" + $env:PATH
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
```

Expected: exits with code 0.

- [ ] **Step 6: Commit**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "输出场景几何体到场景XML"
```

## Task 3: Validate Scene Geometry References and Dimensions

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add failing validation tests**

Add:

```cpp
    {
        RobotModelSpec badGeoRef = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        badGeoRef.sceneGeometries[0].refFrame = "MissingSceneFrame";
        QStringList geoErrors;
        if (RobotModelXmlWriter::validate (badGeoRef, geoErrors))
            return fail ("Scene geometry with missing refframe should fail validation.");
        if (!geoErrors.join (" ").contains ("MissingSceneFrame"))
            return fail ("Scene geometry refframe error should mention MissingSceneFrame.");

        RobotModelSpec badGeoSize = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        badGeoSize.sceneGeometries[0].size = {{0, 0.8, 0.05}};
        QStringList sizeErrors;
        if (RobotModelXmlWriter::validate (badGeoSize, sizeErrors))
            return fail ("Scene geometry with zero Box size should fail validation.");
    }
```

- [ ] **Step 2: Add validation code**

In `validate()`, after scene frame validation, add:

```cpp
    std::set< std::string > sceneFrameRefs;
    sceneFrameRefs.insert ("WORLD");
    sceneFrameRefs.insert ("RobotBase");
    for (const FrameSpec& frame : spec.sceneFrames)
        sceneFrameRefs.insert (frame.name);

    std::set< std::string > sceneGeometryNames;
    for (const SceneGeometrySpec& geometry : spec.sceneGeometries) {
        if (isEmpty (geometry.name))
            errors << "Scene geometry names must not be empty.";
        else if (!sceneGeometryNames.insert (geometry.name).second)
            errors << QString ("Duplicate scene geometry name: %1.")
                          .arg (QString::fromStdString (geometry.name));
        if (isEmpty (geometry.refFrame))
            errors << QString ("Scene geometry %1 requires a refframe.")
                          .arg (QString::fromStdString (geometry.name));
        else if (sceneFrameRefs.find (geometry.refFrame) == sceneFrameRefs.end ())
            errors << QString ("Scene geometry %1 references unknown frame %2.")
                          .arg (QString::fromStdString (geometry.name),
                                QString::fromStdString (geometry.refFrame));
        for (double color : geometry.rgb) {
            if (color < 0 || color > 1)
                errors << QString ("Scene geometry %1 RGB values must be between 0 and 1.")
                              .arg (QString::fromStdString (geometry.name));
        }
        if (geometry.kind == GeometryKind::Box &&
            (!(geometry.size[0] > 0) || !(geometry.size[1] > 0) || !(geometry.size[2] > 0)))
            errors << QString ("Scene geometry %1 Box size must be greater than zero.")
                          .arg (QString::fromStdString (geometry.name));
        if ((geometry.kind == GeometryKind::Cylinder || geometry.kind == GeometryKind::Sphere ||
             geometry.kind == GeometryKind::Cone) && !(geometry.radius > 0))
            errors << QString ("Scene geometry %1 radius must be greater than zero.")
                          .arg (QString::fromStdString (geometry.name));
        if ((geometry.kind == GeometryKind::Cylinder || geometry.kind == GeometryKind::Cone) &&
            !(geometry.length > 0))
            errors << QString ("Scene geometry %1 length must be greater than zero.")
                          .arg (QString::fromStdString (geometry.name));
        if (geometry.kind == GeometryKind::Plane &&
            (!(geometry.size[0] > 0) || !(geometry.size[1] > 0)))
            errors << QString ("Scene geometry %1 Plane size must be greater than zero.")
                          .arg (QString::fromStdString (geometry.name));
        if (geometry.kind == GeometryKind::Mesh && isEmpty (geometry.file))
            errors << QString ("Scene geometry %1 Mesh requires a file path.")
                          .arg (QString::fromStdString (geometry.name));
    }
```

- [ ] **Step 3: Run test and commit**

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release'
$env:PATH="D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\bin;D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWork\bin;D:\software\QT\6.11.1\msvc2022_64\bin;" + $env:PATH
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "校验场景几何体引用和尺寸"
```

## Task 4: Add Scene Geometry UI and Synchronization

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`

- [ ] **Step 1: Add widget members**

In the header add:

```cpp
void addSceneGeometry ();
void removeSelectedSceneGeometry ();
void fillSceneGeometryTable (const RobotModelSpec& spec);
QTableWidget* _sceneGeometryTable;
```

- [ ] **Step 2: Add Scene Geometry table**

In `buildUi()`, place this below `_sceneFramesTable`:

```cpp
_sceneGeometryTable = makeTable (
    QStringList () << "Name"
                   << "RefFrame"
                   << "Kind"
                   << "Size x y z"
                   << "Radius"
                   << "Length"
                   << "File"
                   << "RPY deg (Z Y X)"
                   << "Pos m"
                   << "RGB"
                   << "Collision",
    0);
sceneLay->addWidget (_sceneGeometryTable);
```

Add buttons:

```cpp
QPushButton* addSceneGeometryBtn = new QPushButton ("Add Scene Geometry");
QPushButton* delSceneGeometryBtn = new QPushButton ("Remove Scene Geometry");
sceneBtnLay->addWidget (addSceneGeometryBtn);
sceneBtnLay->addWidget (delSceneGeometryBtn);
connect (addSceneGeometryBtn, SIGNAL (clicked ()), this, SLOT (addSceneGeometry ()));
connect (delSceneGeometryBtn, SIGNAL (clicked ()), this, SLOT (removeSelectedSceneGeometry ()));
```

- [ ] **Step 3: Collect/fill scene geometries**

In `collectSpec()` after scene frame collection:

```cpp
for (int row = 0; row < _sceneGeometryTable->rowCount (); ++row) {
    SceneGeometrySpec geometry;
    geometry.name = itemText (_sceneGeometryTable, row, 0).toStdString ();
    geometry.refFrame = itemText (_sceneGeometryTable, row, 1).toStdString ();
    geometry.kind = geometryKindFromString (itemText (_sceneGeometryTable, row, 2).toStdString ());
    parseVector3 (itemText (_sceneGeometryTable, row, 3), geometry.size);
    geometry.radius = itemDouble (_sceneGeometryTable, row, 4);
    geometry.length = itemDouble (_sceneGeometryTable, row, 5);
    geometry.file = itemText (_sceneGeometryTable, row, 6).toStdString ();
    parseVector3 (itemText (_sceneGeometryTable, row, 7), geometry.rpyDeg);
    parseVector3 (itemText (_sceneGeometryTable, row, 8), geometry.pos);
    parseVector3 (itemText (_sceneGeometryTable, row, 9), geometry.rgb);
    geometry.collisionModel =
        itemText (_sceneGeometryTable, row, 10).compare ("Enabled", Qt::CaseInsensitive) == 0 ||
        itemText (_sceneGeometryTable, row, 10).compare ("true", Qt::CaseInsensitive) == 0;
    spec.sceneGeometries.push_back (geometry);
}
```

Implement fill:

```cpp
void RobotModelBuilderWidget::fillSceneGeometryTable (const RobotModelSpec& spec)
{
    _sceneGeometryTable->setRowCount (static_cast< int >(spec.sceneGeometries.size ()));
    for (int row = 0; row < _sceneGeometryTable->rowCount (); ++row) {
        const SceneGeometrySpec& geometry = spec.sceneGeometries[row];
        setItem (_sceneGeometryTable, row, 0, QString::fromStdString (geometry.name));
        setItem (_sceneGeometryTable, row, 1, QString::fromStdString (geometry.refFrame));
        setItem (_sceneGeometryTable, row, 2, geometryKindToString (geometry.kind));
        setItem (_sceneGeometryTable, row, 3, vectorText (geometry.size));
        setItem (_sceneGeometryTable, row, 4, QString::number (geometry.radius));
        setItem (_sceneGeometryTable, row, 5, QString::number (geometry.length));
        setItem (_sceneGeometryTable, row, 6, QString::fromStdString (geometry.file));
        setItem (_sceneGeometryTable, row, 7, vectorText (geometry.rpyDeg));
        setItem (_sceneGeometryTable, row, 8, vectorText (geometry.pos));
        setItem (_sceneGeometryTable, row, 9, vectorText (geometry.rgb));
        setItem (_sceneGeometryTable, row, 10, geometry.collisionModel ? "Enabled" : "Disabled");
    }
}
```

Call it in `fillFromSpec()` and `generatePreview()` after `fillSceneTab(spec)`.

- [ ] **Step 4: Add add/remove behavior**

```cpp
void RobotModelBuilderWidget::addSceneGeometry ()
{
    RobotModelSpec spec = collectSpec ();
    SceneGeometrySpec geometry;
    geometry.name = "SceneGeometry" + std::to_string (spec.sceneGeometries.size () + 1);
    geometry.refFrame = spec.sceneFrames.empty () ? "RobotBase" : spec.sceneFrames.front ().name;
    geometry.kind = GeometryKind::Box;
    geometry.size = {{0.1, 0.1, 0.1}};
    geometry.radius = 0.05;
    geometry.length = 0.1;
    geometry.rgb = {{0.6, 0.6, 0.6}};
    geometry.collisionModel = true;
    spec.sceneGeometries.push_back (geometry);
    fillFromSpec (spec);
    generatePreview ();
}

void RobotModelBuilderWidget::removeSelectedSceneGeometry ()
{
    RobotModelSpec spec = collectSpec ();
    const int row = _sceneGeometryTable->currentRow ();
    if (row < 0 || row >= static_cast< int >(spec.sceneGeometries.size ()))
        return;
    spec.sceneGeometries.erase (spec.sceneGeometries.begin () + row);
    fillFromSpec (spec);
    generatePreview ();
}
```

- [ ] **Step 5: Synchronize when deleting scene frames**

Update `removeSelectedSceneFrame()`:

```cpp
spec.sceneGeometries.erase (
    std::remove_if (spec.sceneGeometries.begin (), spec.sceneGeometries.end (),
                    [&] (const SceneGeometrySpec& geometry) {
                        return geometry.refFrame == removed;
                    }),
    spec.sceneGeometries.end ());
```

Keep the existing logic that resets child scene frames referencing `removed` to `WORLD`.

- [ ] **Step 6: Build plugin and commit**

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder --config Release'
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp
git commit -m "添加场景几何体编辑界面"
```

## Task 5: Relax Transform Validation in RPYPos Mode

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`

- [ ] **Step 1: Change validation condition**

Replace the unconditional scene frame transform validation:

```cpp
if (!parseVector (itemText (_sceneFramesTable, row, 7), 16))
    errors << QString ("Invalid scene frame Transform vector at row %1.").arg (row + 1);
```

with:

```cpp
const QString poseMode = itemText (_sceneFramesTable, row, 4);
if (poseMode.compare ("Transform4x4", Qt::CaseInsensitive) == 0 ||
    poseMode.compare ("Transform", Qt::CaseInsensitive) == 0) {
    if (!parseVector (itemText (_sceneFramesTable, row, 7), 16))
        errors << QString ("Invalid scene frame Transform vector at row %1.").arg (row + 1);
}
```

- [ ] **Step 2: Build plugin and commit**

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder --config Release'
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp
git commit -m "优化场景位姿输入校验"
```

## Final Verification

- [ ] **Run XML writer tests**

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release'
$env:PATH="D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\bin;D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWork\bin;D:\software\QT\6.11.1\msvc2022_64\bin;" + $env:PATH
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\bin\sdurws_robotmodelbuilder_xmltest.exe"
```

- [ ] **Build plugin**

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder --config Release'
```

- [ ] **Manual acceptance**

1. 打开插件并生成默认预览。
2. Scene XML 中应同时出现 `Table` frame 和 `TableTop` drawable。
3. Scene XML 中应同时出现 `Workpiece` frame 和 `WorkpieceBox` drawable。
4. Scene XML 中应同时出现 `MovableBox` frame 和 `MovableBoxGeom` drawable。
5. 删除 `Table` 后，引用 `Table` 的场景几何体不能残留。
6. 将任意 scene geometry 的 `RefFrame` 改为不存在的名字，保存时必须 validate 报错。

## Commit Message Rule

本计划涉及的所有提交信息必须使用中文，例如：

```bash
git commit -m "添加场景几何数据模型"
git commit -m "输出场景几何体到场景XML"
git commit -m "校验场景几何体引用和尺寸"
git commit -m "添加场景几何体编辑界面"
git commit -m "优化场景位姿输入校验"
```

## Self-Review

- 已覆盖当前代码审查发现的主要缺口：场景 frame 无几何、shape 被忽略、scene geometry 引用校验缺失、删除同步缺失、Transform 校验过严。
- 计划没有使用英文 commit message。
- `FrameSpec` 与 `SceneGeometrySpec` 边界明确，避免把几何字段塞进 frame。
