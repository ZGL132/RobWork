# RobotModelBuilder Scene Frames Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 RobotModelBuilder 增加 Milestone 3 的 Frame / WorkCell 场景对象能力，使 Scene XML 能编辑并输出 RobotBase、Table、Workpiece、CameraFrame、Movable box 等常用 WorkCell 场景 frame，并在校验阶段发现错误引用。

**Architecture:** 数据模型层新增 `FrameSpec` 表达场景 frame；XML writer 负责默认场景、校验和 `makeSceneXml()` 输出；Widget 只负责 Scene Frames 表格与 RobotBase 位姿编辑。设备内部 `transformJoints` 继续只描述 SerialDevice 内部 Base/Joint/TCP/ToolFrame，场景 frame 只存在于 `RobotModelSpec::sceneFrames` 和 `RobotModelSpec::robotBaseFrame`。

**Tech Stack:** C++、Qt Widgets、QString/QTextStream、现有无 GUI 命令行测试 `sdurws_robotmodelbuilder_xmltest`。

---

## Scope and Rules

- 本里程碑不是完整 XML 编辑器，只实现常用 WorkCell 场景构建。
- `RobotBase` 是场景中的机器人安装 frame，不是 SerialDevice 内部 `Base`。
- 设备内部 frame：`Base`、`Joint*`、`TCP`、`ToolFrame`，继续由 `transformJoints` 和 `makeSerialDeviceXml()` 管理。
- 场景 frame：地面、桌子、工件、夹具、相机安装座、机器人安装座等，由 `sceneFrames` 管理。
- 第一阶段 `poseMode` 支持 `RPYPos` 输出；`Transform4x4` 字段先落入数据模型和 UI 校验，XML 输出可以在 Task 3 中实现直接输出 4x4，也可先在校验中拒绝非 `RPYPos`。为了满足本 Milestone 验收，必须完整支持 `RPYPos`。
- 场景 frame 的 `refFrame` 在 Milestone 3 只允许引用 `WORLD`、`RobotBase`、其他 `sceneFrames` 中已存在的 frame。不要允许引用 include 进来的机器人内部 frame，避免跨文件 frame 解析边界不稳定。
- `daf=true` 按用户要求输出为 `<Frame ... daf="true">`。

## Files

- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Modify: `D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`
- No expected CMake changes unless a new source/test file is deliberately created. Prefer not creating new files for this milestone.

## Task 1: Add Scene Frame Data Model

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`

- [ ] **Step 1: Add enums and FrameSpec**

Add these definitions after `enum class JointKind` and before `namespace detail`:

```cpp
enum class SceneFrameType
{
    Normal,
    Fixed,
    Movable
};

enum class PoseMode
{
    RPYPos,
    Transform4x4
};
```

Add this struct after `JointTransformSpec`:

```cpp
struct FrameSpec
{
    std::string name;
    std::string refFrame;
    SceneFrameType frameType = SceneFrameType::Fixed;
    bool daf = false;
    PoseMode poseMode = PoseMode::RPYPos;
    std::array< double, 3 > rpyDeg = {{0, 0, 0}};
    std::array< double, 3 > pos = {{0, 0, 0}};
    std::array< double, 16 > transform = {{1, 0, 0, 0,
                                            0, 1, 0, 0,
                                            0, 0, 1, 0,
                                            0, 0, 0, 1}};
};
```

Add these fields to `RobotModelSpec` after `bool generateScene;`:

```cpp
FrameSpec robotBaseFrame;
std::vector< FrameSpec > sceneFrames;
```

- [ ] **Step 2: Add string conversion helpers**

Still in `RobotModelSpec.hpp`, add inline helpers near `typeToKind()`:

```cpp
inline SceneFrameType sceneFrameTypeFromString (const std::string& type)
{
    const std::string t = detail::trimmed (type);
    if (detail::iequals (t, "Movable"))
        return SceneFrameType::Movable;
    if (detail::iequals (t, "Normal"))
        return SceneFrameType::Normal;
    return SceneFrameType::Fixed;
}

inline const char* sceneFrameTypeToString (SceneFrameType type)
{
    switch (type) {
        case SceneFrameType::Movable: return "Movable";
        case SceneFrameType::Normal: return "Normal";
        case SceneFrameType::Fixed:
        default: return "Fixed";
    }
}

inline PoseMode poseModeFromString (const std::string& mode)
{
    const std::string m = detail::trimmed (mode);
    if (detail::iequals (m, "Transform4x4") || detail::iequals (m, "Transform"))
        return PoseMode::Transform4x4;
    return PoseMode::RPYPos;
}

inline const char* poseModeToString (PoseMode mode)
{
    return mode == PoseMode::Transform4x4 ? "Transform4x4" : "RPYPos";
}
```

- [ ] **Step 3: Commit Task 1**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp
git commit -m "feat(robotmodelbuilder): add scene frame model"
```

## Task 2: Write Milestone 3 Failing Tests

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add scene frame tests before dump-to-temp section**

Insert a new block near the end of `main()`, before the existing dump section:

```cpp
    // =====================================================================
    //  Milestone 3: Frame / WorkCell scene objects
    // =====================================================================
    {
        RobotModelSpec sceneSpec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        sceneSpec.robotBaseFrame.pos = {{0.4, -0.2, 0.75}};
        sceneSpec.robotBaseFrame.rpyDeg = {{0, 0, 90}};

        FrameSpec table;
        table.name = "Table";
        table.refFrame = "WORLD";
        table.frameType = SceneFrameType::Fixed;
        table.rpyDeg = {{0, 0, 0}};
        table.pos = {{0.7, 0, 0.35}};
        sceneSpec.sceneFrames.push_back (table);

        FrameSpec workpiece;
        workpiece.name = "Workpiece";
        workpiece.refFrame = "Table";
        workpiece.frameType = SceneFrameType::Fixed;
        workpiece.daf = true;
        workpiece.rpyDeg = {{0, 0, 0}};
        workpiece.pos = {{0, 0, 0.08}};
        sceneSpec.sceneFrames.push_back (workpiece);

        FrameSpec camera;
        camera.name = "CameraFrame";
        camera.refFrame = "RobotBase";
        camera.frameType = SceneFrameType::Normal;
        camera.rpyDeg = {{180, 0, 0}};
        camera.pos = {{0.2, 0.1, 1.2}};
        sceneSpec.sceneFrames.push_back (camera);

        FrameSpec movableBox;
        movableBox.name = "MovableBox";
        movableBox.refFrame = "WORLD";
        movableBox.frameType = SceneFrameType::Movable;
        movableBox.daf = true;
        movableBox.rpyDeg = {{0, 0, 0}};
        movableBox.pos = {{0.1, 0.2, 0.3}};
        sceneSpec.sceneFrames.push_back (movableBox);

        QStringList sceneErrors;
        if (!RobotModelXmlWriter::validate (sceneSpec, sceneErrors))
            return fail ("Scene frame spec should validate: " + sceneErrors.join ("; "));

        const QString scene = RobotModelXmlWriter::makeSceneXml (sceneSpec);
        if (!contains (scene, "<Frame name=\"RobotBase\" refframe=\"WORLD\">"))
            return fail ("Scene XML should contain RobotBase frame.");
        if (!contains (scene, "<RPY>0 0 90</RPY>\n    <Pos>0.4 -0.2 0.75</Pos>"))
            return fail ("RobotBase pose should be written to Scene XML.");
        if (!contains (scene, "<Frame name=\"Table\" refframe=\"WORLD\" type=\"Fixed\">"))
            return fail ("Scene XML should contain fixed Table frame.");
        if (!contains (scene, "<Frame name=\"Workpiece\" refframe=\"Table\" type=\"Fixed\" daf=\"true\">"))
            return fail ("Scene XML should write daf=true on Workpiece.");
        if (!contains (scene, "<Frame name=\"CameraFrame\" refframe=\"RobotBase\">"))
            return fail ("Normal scene frame should omit type attribute.");
        if (!contains (scene, "<Frame name=\"MovableBox\" refframe=\"WORLD\" type=\"Movable\" daf=\"true\">"))
            return fail ("Movable scene frame with DAF should be written.");
        if (!contains (scene, "<Include file=\"GenericSixAxis.wc.xml\" />"))
            return fail ("Scene XML should still include the robot file.");

        RobotModelSpec badRef = sceneSpec;
        badRef.sceneFrames[0].refFrame = "MissingFrame";
        QStringList badRefErrors;
        if (RobotModelXmlWriter::validate (badRef, badRefErrors))
            return fail ("Scene frame with missing refframe should fail validation.");
        if (!badRefErrors.join (" ").contains ("MissingFrame"))
            return fail ("Missing refframe validation error should mention the bad reference.");
    }
```

- [ ] **Step 2: Run the XML writer test and confirm it fails to compile**

Run from repository root:

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release'
```

Expected: compile failure mentioning `FrameSpec`, `SceneFrameType`, or missing `robotBaseFrame` / `sceneFrames` if Task 1 was not implemented yet. If Task 1 is already committed, expected failure moves to runtime assertions because `validate()` / `makeSceneXml()` do not yet support scene frames.

- [ ] **Step 3: Commit the failing test**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "test(robotmodelbuilder): cover scene frames"
```

## Task 3: Implement Writer Defaults, Validation, and Scene XML Output

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`

- [ ] **Step 1: Add helper declarations**

In `RobotModelXmlWriter.hpp`, add private helpers near `vector3()`:

```cpp
static QString vector16 (const std::array< double, 16 >& values);
static QString frameTypeAttribute (SceneFrameType type);
static void writeFrameXml (QTextStream& out, const FrameSpec& frame, bool showFrameAxes);
```

If `QTextStream` is not visible in the header, add a forward declaration before namespace `rws`:

```cpp
class QTextStream;
```

- [ ] **Step 2: Initialize default RobotBase and common scene frames**

In `RobotModelXmlWriter::makeDefaultSixAxisModel()`, after base booleans are set and before returning `spec`, initialize:

```cpp
spec.robotBaseFrame.name = "RobotBase";
spec.robotBaseFrame.refFrame = "WORLD";
spec.robotBaseFrame.frameType = SceneFrameType::Fixed;
spec.robotBaseFrame.daf = false;
spec.robotBaseFrame.poseMode = PoseMode::RPYPos;
spec.robotBaseFrame.rpyDeg = {{0, 0, 0}};
spec.robotBaseFrame.pos = {{0, 0, 0}};

FrameSpec table;
table.name = "Table";
table.refFrame = "WORLD";
table.frameType = SceneFrameType::Fixed;
table.pos = {{0.7, 0, 0.35}};
spec.sceneFrames.push_back (table);

FrameSpec workpiece;
workpiece.name = "Workpiece";
workpiece.refFrame = "Table";
workpiece.frameType = SceneFrameType::Fixed;
workpiece.daf = true;
workpiece.pos = {{0, 0, 0.08}};
spec.sceneFrames.push_back (workpiece);

FrameSpec camera;
camera.name = "CameraFrame";
camera.refFrame = "RobotBase";
camera.frameType = SceneFrameType::Normal;
camera.rpyDeg = {{180, 0, 0}};
camera.pos = {{0.2, 0.1, 1.2}};
spec.sceneFrames.push_back (camera);

FrameSpec movableBox;
movableBox.name = "MovableBox";
movableBox.refFrame = "WORLD";
movableBox.frameType = SceneFrameType::Movable;
movableBox.daf = true;
movableBox.pos = {{0.1, 0.2, 0.3}};
spec.sceneFrames.push_back (movableBox);
```

Keep defaults modest. Do not add drawables for these objects in Milestone 3 unless the current code already has a scene-object drawable concept.

- [ ] **Step 3: Implement helper functions**

In `RobotModelXmlWriter.cpp`, add helper implementations near existing `vector3()` / `number()` definitions:

```cpp
QString RobotModelXmlWriter::vector16 (const std::array< double, 16 >& values)
{
    QStringList parts;
    for (double value : values)
        parts << number (value);
    return parts.join (" ");
}

QString RobotModelXmlWriter::frameTypeAttribute (SceneFrameType type)
{
    if (type == SceneFrameType::Movable)
        return " type=\"Movable\"";
    if (type == SceneFrameType::Fixed)
        return " type=\"Fixed\"";
    return QString ();
}

void RobotModelXmlWriter::writeFrameXml (QTextStream& out, const FrameSpec& frame,
                                         bool showFrameAxes)
{
    out << "  <Frame name=\"" << QString::fromStdString (frame.name)
        << "\" refframe=\"" << QString::fromStdString (frame.refFrame) << "\""
        << frameTypeAttribute (frame.frameType);
    if (frame.daf)
        out << " daf=\"true\"";
    out << ">\n";

    if (frame.poseMode == PoseMode::Transform4x4) {
        out << "    <Transform>" << vector16 (frame.transform) << "</Transform>\n";
    }
    else {
        out << "    <RPY>" << vector3 (frame.rpyDeg) << "</RPY>\n";
        out << "    <Pos>" << vector3 (frame.pos) << "</Pos>\n";
    }

    if (showFrameAxes)
        out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
    out << "  </Frame>\n";
}
```

If RobWork's accepted 4x4 tag differs from `<Transform>`, prefer rejecting `Transform4x4` in validation for Milestone 3 and keep the field for future work. The acceptance tests above only require `RPYPos`.

- [ ] **Step 4: Extend validation for scene frames**

In `RobotModelXmlWriter::validate()`, after device frame name collection and before drawables validation, add:

```cpp
    if (isEmpty (spec.robotBaseFrame.name))
        errors << "RobotBase frame name must not be empty.";
    if (spec.robotBaseFrame.name != "RobotBase")
        errors << "RobotBase frame name must be RobotBase.";
    if (spec.robotBaseFrame.refFrame != "WORLD")
        errors << "RobotBase refframe must be WORLD.";

    std::set< std::string > sceneNames;
    sceneNames.insert ("WORLD");
    sceneNames.insert ("RobotBase");

    for (const FrameSpec& frame : spec.sceneFrames) {
        if (isEmpty (frame.name))
            errors << "Scene frame names must not be empty.";
        else if (!sceneNames.insert (frame.name).second)
            errors << QString ("Duplicate scene frame name: %1.")
                          .arg (QString::fromStdString (frame.name));

        if (allNames.find (frame.name) != allNames.end () || frame.name == "Base" ||
            frame.name == "TCP")
            errors << QString ("Scene frame %1 collides with a device frame name.")
                          .arg (QString::fromStdString (frame.name));
    }

    std::set< std::string > availableSceneRefs;
    availableSceneRefs.insert ("WORLD");
    availableSceneRefs.insert ("RobotBase");
    for (const FrameSpec& frame : spec.sceneFrames)
        availableSceneRefs.insert (frame.name);

    for (const FrameSpec& frame : spec.sceneFrames) {
        if (isEmpty (frame.refFrame))
            errors << QString ("Scene frame %1 requires a refframe.")
                          .arg (QString::fromStdString (frame.name));
        else if (availableSceneRefs.find (frame.refFrame) == availableSceneRefs.end ())
            errors << QString ("Scene frame %1 references unknown refframe %2.")
                          .arg (QString::fromStdString (frame.name),
                                QString::fromStdString (frame.refFrame));
        if (frame.name == frame.refFrame)
            errors << QString ("Scene frame %1 must not reference itself.")
                          .arg (QString::fromStdString (frame.name));
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

Important: this validation allows forward references among `sceneFrames`. If execution wants stricter output order, reject forward references instead. For this Milestone, forward references are acceptable because XML contains all frames in one WorkCell.

- [ ] **Step 5: Update `makeSceneXml()`**

Replace the current hard-coded `RobotBase` body with:

```cpp
QString RobotModelXmlWriter::makeSceneXml (const RobotModelSpec& spec)
{
    const QString robotName = exportedRobotName (spec);
    QString xml;
    QTextStream out (&xml);
    out << "<WorkCell name=\"" << robotName << "Scene\">\n";

    FrameSpec robotBase = spec.robotBaseFrame;
    if (robotBase.name.empty ())
        robotBase.name = "RobotBase";
    if (robotBase.refFrame.empty ())
        robotBase.refFrame = "WORLD";
    robotBase.frameType = SceneFrameType::Fixed;
    writeFrameXml (out, robotBase, spec.showFrameAxes);
    out << "\n";

    for (const FrameSpec& frame : spec.sceneFrames)
        writeFrameXml (out, frame, spec.showFrameAxes);
    if (!spec.sceneFrames.empty ())
        out << "\n";

    out << "  <Include file=\"" << robotName << ".wc.xml\" />\n";
    out << "</WorkCell>\n";
    return xml;
}
```

- [ ] **Step 6: Run tests**

Build:

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release'
```

Run:

```powershell
$env:PATH="D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\bin;D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWork\bin;D:\software\QT\6.11.1\msvc2022_64\bin;" + $env:PATH
.\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\src\rwslibs\robotmodelbuilder\Release\sdurws_robotmodelbuilder_xmltest.exe
```

Expected: all tests pass, including the new Milestone 3 block.

- [ ] **Step 7: Commit Task 3**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp
git commit -m "feat(robotmodelbuilder): write scene frames"
```

## Task 4: Add Widget Scene UI

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`

- [ ] **Step 1: Add widget members and slots**

In `RobotModelBuilderWidget.hpp`, add slots:

```cpp
void addSceneFrame ();
void removeSelectedSceneFrame ();
```

Add private helpers:

```cpp
void fillSceneTab (const RobotModelSpec& spec);
static bool parseVector16 (const QString& text, std::array< double, 16 >& values);
static QString vectorText16 (const std::array< double, 16 >& values);
```

Add members:

```cpp
QLineEdit* _robotBaseRpy;
QLineEdit* _robotBasePos;
QTableWidget* _sceneFramesTable;
```

- [ ] **Step 2: Build the Scene tab**

In `buildUi()`, after Drawables tab and before Limits tab, add:

```cpp
    QWidget* sceneTab = new QWidget ();
    QVBoxLayout* sceneLayout = new QVBoxLayout (sceneTab);
    QFormLayout* robotBaseForm = new QFormLayout ();
    _robotBaseRpy = new QLineEdit ();
    _robotBasePos = new QLineEdit ();
    robotBaseForm->addRow ("RobotBase RPY deg (Z Y X)", _robotBaseRpy);
    robotBaseForm->addRow ("RobotBase Pos m", _robotBasePos);
    sceneLayout->addLayout (robotBaseForm);

    _sceneFramesTable = makeTable (
        QStringList () << "Name"
                       << "RefFrame"
                       << "Type"
                       << "DAF"
                       << "PoseMode"
                       << "RPY deg (Z Y X)"
                       << "Pos m"
                       << "Transform 4x4",
        0);
    sceneLayout->addWidget (_sceneFramesTable);

    QWidget* sceneButtons = new QWidget ();
    QHBoxLayout* sceneBtnLay = new QHBoxLayout (sceneButtons);
    QPushButton* addSceneBtn = new QPushButton ("Add Scene Frame");
    QPushButton* delSceneBtn = new QPushButton ("Remove Scene Frame");
    sceneBtnLay->setContentsMargins (0, 0, 0, 0);
    sceneBtnLay->addWidget (addSceneBtn);
    sceneBtnLay->addWidget (delSceneBtn);
    sceneBtnLay->addStretch ();
    sceneLayout->addWidget (sceneButtons);
    tabs->addTab (sceneTab, "Scene Frames");
```

Add signal connections near existing button connects:

```cpp
connect (addSceneBtn, SIGNAL (clicked ()), this, SLOT (addSceneFrame ()));
connect (delSceneBtn, SIGNAL (clicked ()), this, SLOT (removeSelectedSceneFrame ()));
```

- [ ] **Step 3: Fill and collect Scene tab**

In `fillFromSpec()`, after `fillDrawablesTable(spec);`, call:

```cpp
    fillSceneTab (spec);
```

Implement:

```cpp
void RobotModelBuilderWidget::fillSceneTab (const RobotModelSpec& spec)
{
    setItemText (_robotBaseRpy, vectorText (spec.robotBaseFrame.rpyDeg));
    setItemText (_robotBasePos, vectorText (spec.robotBaseFrame.pos));

    _sceneFramesTable->setRowCount (static_cast< int >(spec.sceneFrames.size ()));
    for (int row = 0; row < _sceneFramesTable->rowCount (); ++row) {
        const FrameSpec& frame = spec.sceneFrames[row];
        setItem (_sceneFramesTable, row, 0, QString::fromStdString (frame.name));
        setItem (_sceneFramesTable, row, 1, QString::fromStdString (frame.refFrame));
        setItem (_sceneFramesTable, row, 2, sceneFrameTypeToString (frame.frameType));
        setItem (_sceneFramesTable, row, 3, frame.daf ? "true" : "false");
        setItem (_sceneFramesTable, row, 4, poseModeToString (frame.poseMode));
        setItem (_sceneFramesTable, row, 5, vectorText (frame.rpyDeg));
        setItem (_sceneFramesTable, row, 6, vectorText (frame.pos));
        setItem (_sceneFramesTable, row, 7, vectorText16 (frame.transform));
    }
}
```

If `setItemText()` does not exist, use:

```cpp
_robotBaseRpy->setText (vectorText (spec.robotBaseFrame.rpyDeg));
_robotBasePos->setText (vectorText (spec.robotBaseFrame.pos));
```

In `collectSpec()`, after drawables collection and before limits collection, add:

```cpp
    spec.robotBaseFrame.name = "RobotBase";
    spec.robotBaseFrame.refFrame = "WORLD";
    spec.robotBaseFrame.frameType = SceneFrameType::Fixed;
    spec.robotBaseFrame.daf = false;
    spec.robotBaseFrame.poseMode = PoseMode::RPYPos;
    parseVector3 (_robotBaseRpy->text (), spec.robotBaseFrame.rpyDeg);
    parseVector3 (_robotBasePos->text (), spec.robotBaseFrame.pos);

    for (int row = 0; row < _sceneFramesTable->rowCount (); ++row) {
        FrameSpec frame;
        frame.name = itemText (_sceneFramesTable, row, 0).toStdString ();
        frame.refFrame = itemText (_sceneFramesTable, row, 1).toStdString ();
        frame.frameType = sceneFrameTypeFromString (itemText (_sceneFramesTable, row, 2).toStdString ());
        frame.daf = itemText (_sceneFramesTable, row, 3).compare ("true", Qt::CaseInsensitive) == 0 ||
                    itemText (_sceneFramesTable, row, 3).compare ("Enabled", Qt::CaseInsensitive) == 0;
        frame.poseMode = poseModeFromString (itemText (_sceneFramesTable, row, 4).toStdString ());
        parseVector3 (itemText (_sceneFramesTable, row, 5), frame.rpyDeg);
        parseVector3 (itemText (_sceneFramesTable, row, 6), frame.pos);
        parseVector16 (itemText (_sceneFramesTable, row, 7), frame.transform);
        spec.sceneFrames.push_back (frame);
    }
```

- [ ] **Step 4: Add add/remove scene frame behavior**

Implement:

```cpp
void RobotModelBuilderWidget::addSceneFrame ()
{
    RobotModelSpec spec = collectSpec ();
    FrameSpec frame;
    frame.name = "SceneFrame" + std::to_string (spec.sceneFrames.size () + 1);
    frame.refFrame = "WORLD";
    frame.frameType = SceneFrameType::Fixed;
    frame.daf = false;
    frame.poseMode = PoseMode::RPYPos;
    frame.rpyDeg = {{0, 0, 0}};
    frame.pos = {{0, 0, 0}};
    spec.sceneFrames.push_back (frame);
    fillFromSpec (spec);
    generatePreview ();
}

void RobotModelBuilderWidget::removeSelectedSceneFrame ()
{
    RobotModelSpec spec = collectSpec ();
    const int row = _sceneFramesTable->currentRow ();
    if (row < 0 || row >= static_cast< int >(spec.sceneFrames.size ()))
        return;
    const std::string removed = spec.sceneFrames[row].name;
    spec.sceneFrames.erase (spec.sceneFrames.begin () + row);
    for (FrameSpec& frame : spec.sceneFrames) {
        if (frame.refFrame == removed)
            frame.refFrame = "WORLD";
    }
    fillFromSpec (spec);
    generatePreview ();
}
```

This is the same synchronization principle as joint deletion: when a scene frame is removed, dependent scene frames must not keep a dangling `refFrame`.

- [ ] **Step 5: Add vector16 helpers and UI validation**

Implement `parseVector16()` using the same pattern as `parseVector3()` / `parseVector6()`:

```cpp
bool RobotModelBuilderWidget::parseVector16 (const QString& text,
                                             std::array< double, 16 >& values)
{
    QStringList parts = text.split (QRegularExpression ("\\s+"), Qt::SkipEmptyParts);
    if (parts.size () != 16)
        return false;
    for (int i = 0; i < 16; ++i) {
        bool ok = false;
        const double v = parts[i].toDouble (&ok);
        if (!ok)
            return false;
        values[i] = v;
    }
    return true;
}

QString RobotModelBuilderWidget::vectorText16 (const std::array< double, 16 >& values)
{
    QStringList parts;
    for (double value : values)
        parts << QString::number (value);
    return parts.join (" ");
}
```

In `validateTableInput()`, add:

```cpp
    if (!parseVector (_robotBaseRpy->text (), 3))
        errors << "Invalid RobotBase RPY vector.";
    if (!parseVector (_robotBasePos->text (), 3))
        errors << "Invalid RobotBase Pos vector.";

    for (int row = 0; row < _sceneFramesTable->rowCount (); ++row) {
        if (!parseVector (itemText (_sceneFramesTable, row, 5), 3))
            errors << QString ("Invalid scene frame RPY vector at row %1.").arg (row + 1);
        if (!parseVector (itemText (_sceneFramesTable, row, 6), 3))
            errors << QString ("Invalid scene frame Pos vector at row %1.").arg (row + 1);
        if (!parseVector (itemText (_sceneFramesTable, row, 7), 16))
            errors << QString ("Invalid scene frame Transform vector at row %1.").arg (row + 1);
    }
```

If existing `parseVector()` is not visible for 16 elements, call `parseVector16()` instead.

- [ ] **Step 6: Build plugin**

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder --config Release'
```

Expected: target builds without Qt MOC errors. If MOC errors mention missing slots, re-run CMake configure or check `Q_OBJECT` slot declarations.

- [ ] **Step 7: Commit Task 4**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp
git commit -m "feat(robotmodelbuilder): add scene frames UI"
```

## Task 5: Final Regression and Manual Acceptance

**Files:**
- No new files expected.

- [ ] **Step 1: Run XML writer test**

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder_xmltest --config Release'
$env:PATH="D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\bin;D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWork\bin;D:\software\QT\6.11.1\msvc2022_64\bin;" + $env:PATH
.\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\RobWorkStudio\src\rwslibs\robotmodelbuilder\Release\sdurws_robotmodelbuilder_xmltest.exe
```

Expected: executable exits with code 0 and prints dump paths.

- [ ] **Step 2: Build plugin target**

```powershell
cmd.exe /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" --target sdurws_robotmodelbuilder --config Release'
```

Expected: build succeeds.

- [ ] **Step 3: Manual UI acceptance**

Open RobWorkStudio with the built plugin and verify:

1. Scene Frames tab is visible.
2. RobotBase RPY/Pos fields show `0 0 0` and `0 0 0` by default.
3. Default Scene Frames include `Table`, `Workpiece`, `CameraFrame`, `MovableBox`.
4. Change RobotBase Pos to `0.4 -0.2 0.75` and RPY to `0 0 90`.
5. Click Generate Preview.
6. Scene XML preview contains:

```xml
<Frame name="RobotBase" refframe="WORLD" type="Fixed">
    <RPY>0 0 90</RPY>
    <Pos>0.4 -0.2 0.75</Pos>
```

7. `Workpiece` contains `daf="true"`.
8. Set `Table` refframe to `MissingFrame`, click Save XML, and confirm validation fails with an error mentioning `MissingFrame`.
9. Remove a scene frame that another scene frame references and confirm the dependent frame's `RefFrame` is reset to `WORLD`.

- [ ] **Step 4: Commit final test adjustments if any**

```bash
git status --short
git add RobWorkStudio/src/rwslibs/robotmodelbuilder
git commit -m "test(robotmodelbuilder): verify scene frame workflow"
```

Only run this commit if manual acceptance required small fixes after Task 4.

## Risk Notes for Implementers

- 当前源码注释里可能有编码损坏，不要在无关区域大范围重写注释。
- 不要把 `sceneFrames` 加进 `allFrameNames(spec)` 给设备 drawable 使用，除非明确决定机器人本体 drawable 可以引用场景 frame。本 Milestone 建议保持设备 drawable 只引用设备内部 frame。
- 不要在删除关节时同步删除场景 frame。场景 frame 和关节表是不同边界；只需要继续保证 Milestone 1+2 的 limits、poses、dynamics、默认 drawables 同步。
- 删除场景 frame 时必须同步修正其他 scene frame 的 `refFrame`，否则会留下 dangling reference。
- `RobotBase` 的 `type="Fixed"` 是否输出取决于 RobWork loader 支持；本计划按用户需求和 Scene Frames 类型显式输出。若 loader 不接受 `type="Fixed"`，保留普通 `<Frame>`，但测试与验收需要同步改为检查没有 type 属性。
- 如果 `<Transform>` 不是 RobWork `.wc.xml` 接受的标签，不要强行输出未知 XML。可在 Milestone 3 中只允许 `RPYPos`，把 `Transform4x4` 留到后续 milestone。

## Self-Review

- Spec coverage:
  - `FrameSpec` 字段已在 Task 1 覆盖。
  - 设备内部 frame 与场景 frame 边界已在 Scope 和 Task 3/4 覆盖。
  - Scene UI 的 Scene Frames 表已在 Task 4 覆盖。
  - RobotBase RPY/Pos 编辑已在 Task 4 覆盖。
  - `makeSceneXml()` 输出 RobotBase、Include、scene frames、Movable/DAF 已在 Task 3 覆盖。
  - 验收测试已在 Task 2 和 Task 5 覆盖。
- Placeholder scan:
  - 本计划没有 `TBD`、`TODO`、`implement later`。
- Type consistency:
  - `SceneFrameType`、`PoseMode`、`FrameSpec` 字段名在所有任务中一致。
