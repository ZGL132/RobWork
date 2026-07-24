# Robot Model Builder Joint Types Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 完善 RobotModelBuilder 的关节/帧类型支持，使同一条运动学链能够明确表达 `Revolute`、`Prismatic`、`FixedFrame`、`ToolFrame/TCP`，并让 DH 表清楚展示 Prismatic 时哪些参数是固定几何、哪个参数是运动变量。

**Architecture:** 以 `Joint + RPY + Pos` 作为完整运动学事实来源，DH 表作为可编辑的简化参数视图和投影视图。`Revolute` 与 `Prismatic` 输出为 RobWork `<Joint>`，`FixedFrame` 与 `ToolFrame` 输出为 `<Frame>`，限位和 `<Q>` 只包含可动关节。纯 6 轴 Revolute 且选择 DH 模式时可继续保留 `<DHJoint type="schilling">` 输出以保持兼容，一旦出现 Prismatic/Fixed/Tool 则降级到通用 `<Joint>/<Frame>` 链式输出。

**Tech Stack:** C++17 风格 STL 数据结构、Qt Widgets `QTableWidget`/delegate、QtCore XML 字符串生成、现有 `sdurws_robotmodelbuilder_xmltest` 命令行回归测试、RobWork XML `<Joint>`/`<Frame>`/`<PosLimit>`/`<Q>`。

---

## 关键语义约定

### 关节/帧类型

| UI 类型 | XML 输出 | 是否进入 Q | 限位单位 | 运动轴/变量 |
| --- | --- | --- | --- | --- |
| `Revolute` | `<Joint type="Revolute">` 或纯 DH 模式下 `<DHJoint type="schilling">` | 是 | deg、deg/s、deg/s2 | 绕该关节局部 Z 轴旋转, 变量是 `theta` |
| `Prismatic` | `<Joint type="Prismatic">` | 是 | m、m/s、m/s2 | 沿该关节局部 Z 轴平移, 变量是 `d` |
| `FixedFrame` | `<Frame type="Fixed">` 或普通 `<Frame>` | 否 | 无 | 固定坐标变换 |
| `ToolFrame` | `<Frame name="TCP"...>` 或用户命名 tool frame | 否 | 无 | 固定末端工具/TCP 变换 |

### DH 表参数含义

| 类型 | `alpha deg` | `a m` | `d m` | `offset deg` | DH 表中的运动变量提示 |
| --- | --- | --- | --- | --- | --- |
| `Revolute` | 固定扭转角 | 固定连杆长度 | 固定 Z 偏移 | `theta0`, 零位角偏置 | `theta = q + offset` |
| `Prismatic` | 固定扭转角 | 固定连杆长度 | `d0`, 零位平移偏置 | 固定 `theta` | `d = q + d0` |
| `FixedFrame` | 固定姿态投影 | 固定 XY 投影长度 | 固定 Z 偏移 | 固定 XY 方向角 | `none` |
| `ToolFrame` | 固定姿态投影 | 固定 XY 投影长度 | 固定 Z 偏移 | 固定 XY 方向角 | `tool` |

### RPY/Pos 到 DH 投影

继续沿用现有投影规则:

```text
a         = sqrt(pos.x^2 + pos.y^2)
offsetDeg = atan2(pos.y, pos.x)
d         = pos.z
alphaDeg  = rpy.z
```

当 `pitch != 0` 或 `rpy.x != atan2(pos.y, pos.x)` 时，DH 是有损投影。Prismatic、FixedFrame、ToolFrame 也允许投影，但 UI 状态栏必须提示“DH 参数是投影值，不是完整姿态”。

---

## 文件结构

### 修改文件

- `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelSpec.hpp`
  - 增加标准化的运动学行类型枚举和 helper。
  - 给 `DHJointSpec` 增加 `type`。
  - 将限位字段从 `Deg` 命名改为中性单位。
  - 将 `PoseSpec` 从固定 6 维数组改为按可动关节数量变化的 `std::vector<double>`。

- `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelXmlWriter.hpp`
  - 暴露 active joint 统计、单位转换、混合链输出相关 helper。
  - 更新 `transformJointToDh`/`dhJointToTransform` 注释，明确 Prismatic 语义。

- `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelXmlWriter.cpp`
  - 默认模型仍是 6 个 Revolute。
  - `validate()` 不再要求恰好 6 个 active joints。
  - `makeSerialDeviceXml()` 根据类型输出 `<Joint>` 或 `<Frame>`。
  - `<PosLimit>/<VelLimit>/<AccLimit>` 和 `<Q>` 按关节类型选择单位转换。
  - `computeLinkPose()` 和 `applyLinkGeometry()` 使用运动学行序列，不假设固定 6 行。

- `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelBuilderWidget.hpp`
  - 增加 Kinematics 行增删 slot。
  - 增加更新 Limits/Poses/Dynamics 表头和行的 helper。

- `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelBuilderWidget.cpp`
  - DH 表增加 `Type` 和 `Active variable` 列。
  - Transform 表 Type 列改成受限选择。
  - Limits/Poses 表根据可动关节动态更新。
  - Prismatic 的 UI 单位显示为 m，Revolute 显示为 deg。

- `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelXmlWriterTest.cpp`
  - 新增 Prismatic、FixedFrame、ToolFrame、混合 Q 向量和 DH 投影视图测试。

- `D:\10_Source_Repos\21_robot\RobWork\RobWork\docs\RobotModelBuilder.md`
  - 更新关节类型、DH 参数意义、单位规则和 XML 示例。

---

### Task 1: 数据模型增加标准关节类型

**Files:**
- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelSpec.hpp`
- Test: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: 在测试中先写类型归一化用例**

在 `RobotModelXmlWriterTest.cpp` 的转换测试区域附近添加:

```cpp
{
    if (kinematicRowTypeFromString ("revolute") != KinematicRowType::Revolute)
        return fail ("kinematicRowTypeFromString should accept lowercase revolute.");
    if (kinematicRowTypeFromString ("Prismatic") != KinematicRowType::Prismatic)
        return fail ("kinematicRowTypeFromString should parse Prismatic.");
    if (kinematicRowTypeFromString ("Fixed") != KinematicRowType::FixedFrame)
        return fail ("kinematicRowTypeFromString should accept Fixed alias.");
    if (kinematicRowTypeFromString ("TCP") != KinematicRowType::ToolFrame)
        return fail ("kinematicRowTypeFromString should accept TCP alias.");
    if (!isMovableJointType (KinematicRowType::Revolute))
        return fail ("Revolute should be movable.");
    if (!isMovableJointType (KinematicRowType::Prismatic))
        return fail ("Prismatic should be movable.");
    if (isMovableJointType (KinematicRowType::FixedFrame))
        return fail ("FixedFrame should not be movable.");
    if (isMovableJointType (KinematicRowType::ToolFrame))
        return fail ("ToolFrame should not be movable.");
}
```

- [ ] **Step 2: 运行测试并确认失败**

Run:

```powershell
cmd /s /c "call ""D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake --build ""D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release"" --target sdurws_robotmodelbuilder_xmltest --config Release"
```

Expected: 编译失败，提示 `KinematicRowType` 或 helper 未声明。

- [ ] **Step 3: 在 `RobotModelSpec.hpp` 增加类型定义**

在 `RobotModelMode` 后添加:

```cpp
enum class KinematicRowType
{
    Revolute,
    Prismatic,
    FixedFrame,
    ToolFrame,
    Unknown
};

inline std::string normalizedTypeText (const std::string& type)
{
    std::string value;
    value.reserve (type.size ());
    for (char ch : type) {
        if (ch != ' ' && ch != '_' && ch != '-')
            value.push_back (static_cast< char > (std::tolower (static_cast< unsigned char > (ch))));
    }
    return value;
}

inline KinematicRowType kinematicRowTypeFromString (const std::string& type)
{
    const std::string value = normalizedTypeText (type);
    if (value == "revolute")
        return KinematicRowType::Revolute;
    if (value == "prismatic")
        return KinematicRowType::Prismatic;
    if (value == "fixed" || value == "fixedframe" || value == "frame")
        return KinematicRowType::FixedFrame;
    if (value == "tcp" || value == "tool" || value == "toolframe")
        return KinematicRowType::ToolFrame;
    return KinematicRowType::Unknown;
}

inline const char* kinematicRowTypeName (KinematicRowType type)
{
    switch (type) {
        case KinematicRowType::Revolute: return "Revolute";
        case KinematicRowType::Prismatic: return "Prismatic";
        case KinematicRowType::FixedFrame: return "FixedFrame";
        case KinematicRowType::ToolFrame: return "ToolFrame";
        default: return "Unknown";
    }
}

inline bool isMovableJointType (KinematicRowType type)
{
    return type == KinematicRowType::Revolute || type == KinematicRowType::Prismatic;
}

inline bool isFrameRowType (KinematicRowType type)
{
    return type == KinematicRowType::FixedFrame || type == KinematicRowType::ToolFrame;
}
```

同时给 `RobotModelSpec.hpp` 增加 include:

```cpp
#include <cctype>
```

- [ ] **Step 4: 给 DH、Limit、Pose 改为中性字段**

修改结构体:

```cpp
struct DHJointSpec
{
    std::string name;
    std::string type = "Revolute";
    double alphaDeg;
    double a;
    double d;
    double offsetDeg;
};

struct JointLimitSpec
{
    std::string jointName;
    double posMin;
    double posMax;
    double velMax;
    double accMax;
};

struct PoseSpec
{
    std::string name;
    std::vector< double > q;
};
```

说明: `posMin/posMax/velMax/accMax` 的单位由对应行类型决定。Revolute 使用 deg，Prismatic 使用 m。`PoseSpec::q` 的长度必须等于 active movable joints 数量。

- [ ] **Step 5: 临时修复编译引用**

把当前工程中所有旧字段替换为新字段:

```text
posMinDeg -> posMin
posMaxDeg -> posMax
velMaxDeg -> velMax
accMaxDeg -> accMax
qDeg      -> q
```

默认模型中:

```cpp
PoseSpec zero;
zero.name = "Zero";
zero.q    = {0, 0, 0, 0, 0, 0};

PoseSpec ready;
ready.name = "Ready";
ready.q    = {0, -90, 90, 0, 0, 0};
```

- [ ] **Step 6: 运行测试并确认通过到现有断言**

Run 同 Step 2。

Expected: 编译通过。若测试运行阶段仍因本机运行时环境返回 exit code 1 且无 stdout/stderr，记录该环境问题，但必须至少确认目标成功编译链接。

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "完善机器人模型构建器的关节类型数据结构"
```

---

### Task 2: XML Writer 支持 active joint 统计和单位转换

**Files:**
- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelXmlWriter.hpp`
- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelXmlWriter.cpp`
- Test: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: 写 Prismatic 序列化测试**

在 `RobotModelXmlWriterTest.cpp` 中默认 XML 测试后添加:

```cpp
{
    RobotModelSpec prismatic = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    prismatic.mode = RobotModelMode::JointRPYPos;
    prismatic.transformJoints[1].type = "Prismatic";
    prismatic.dhJoints[1].type = "Prismatic";
    prismatic.limits[1].posMin = 0.0;
    prismatic.limits[1].posMax = 0.4;
    prismatic.limits[1].velMax = 0.5;
    prismatic.limits[1].accMax = 1.25;
    prismatic.poses[0].q = {0, 0.125, 0, 0, 0, 0};

    QStringList errors;
    if (!RobotModelXmlWriter::validate (prismatic, errors))
        return fail ("Prismatic model should validate: " + errors.join ("; "));

    const QString xml = RobotModelXmlWriter::makeSerialDeviceXml (prismatic);
    if (!contains (xml, "<Joint name=\"Joint2\" type=\"Prismatic\">"))
        return fail ("Prismatic joint should serialize as Joint type Prismatic.");
    if (!contains (xml, "<PosLimit refjoint=\"Joint2\" min=\"0\" max=\"0.4\" />"))
        return fail ("Prismatic position limits should be written in meters without degree conversion.");
    if (!contains (xml, "<VelLimit refjoint=\"Joint2\" max=\"0.5\" />"))
        return fail ("Prismatic velocity limit should be written in m/s.");
    if (!contains (xml, "<AccLimit refjoint=\"Joint2\" max=\"1.25\" />"))
        return fail ("Prismatic acceleration limit should be written in m/s2.");
    if (!contains (xml, "<Q name=\"Zero\">0 0.125 0 0 0 0</Q>"))
        return fail ("Prismatic Q value should stay linear while Revolute values are converted as needed.");
}
```

注意: 如果现有 `number()` 输出精度导致字符串为 `0.125000...`，不要放宽整个测试。只为数字格式增加局部 helper，例如 `containsQValueAt(...)`，避免误判 XML 结构。

- [ ] **Step 2: 运行测试确认失败**

Run:

```powershell
cmd /s /c "call ""D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake --build ""D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release"" --target sdurws_robotmodelbuilder_xmltest --config Release"
```

Expected: 测试失败，当前 `<Q>` 会把所有值按角度转弧度，或 validate/字段引用不支持 Prismatic 单位。

- [ ] **Step 3: 在 Writer 中增加 active row helper**

在 `RobotModelXmlWriter.hpp` public 区域添加:

```cpp
static std::vector< JointTransformSpec > effectiveKinematicRows (const RobotModelSpec& spec);
static std::vector< JointTransformSpec > activeJointRows (const RobotModelSpec& spec);
static bool hasOnlyRevoluteDhRows (const RobotModelSpec& spec);
```

在 `RobotModelXmlWriter.cpp` 实现:

```cpp
std::vector< JointTransformSpec > RobotModelXmlWriter::effectiveKinematicRows (const RobotModelSpec& spec)
{
    if (spec.mode == RobotModelMode::JointRPYPos)
        return spec.transformJoints;

    std::vector< JointTransformSpec > rows;
    rows.reserve (spec.dhJoints.size ());
    const size_t n = std::min (spec.dhJoints.size (), spec.transformJoints.size ());
    for (size_t i = 0; i < spec.dhJoints.size (); ++i) {
        std::string type = spec.dhJoints[i].type;
        if (type.empty () && i < n)
            type = spec.transformJoints[i].type;
        rows.push_back (dhJointToTransform (spec.dhJoints[i], type));
    }
    return rows;
}

std::vector< JointTransformSpec > RobotModelXmlWriter::activeJointRows (const RobotModelSpec& spec)
{
    std::vector< JointTransformSpec > active;
    for (const JointTransformSpec& row : effectiveKinematicRows (spec)) {
        if (isMovableJointType (kinematicRowTypeFromString (row.type)))
            active.push_back (row);
    }
    return active;
}

bool RobotModelXmlWriter::hasOnlyRevoluteDhRows (const RobotModelSpec& spec)
{
    if (spec.mode != RobotModelMode::DH)
        return false;
    for (const DHJointSpec& row : spec.dhJoints) {
        if (kinematicRowTypeFromString (row.type) != KinematicRowType::Revolute)
            return false;
    }
    return true;
}
```

- [ ] **Step 4: 增加单位转换 helper**

在 `RobotModelXmlWriter.cpp` 私有匿名 namespace 增加:

```cpp
double qValueForXml (double uiValue, KinematicRowType type)
{
    if (type == KinematicRowType::Revolute)
        return uiValue * Pi / 180.0;
    return uiValue;
}
```

`PosLimit/VelLimit/AccLimit` 保持 UI 原单位写出。现有 RobWork 示例对 Revolute limit 使用度，Prismatic limit 使用米，不要把 Revolute limit 改成弧度。

- [ ] **Step 5: 更新 validate**

`validate()` 规则改为:

```text
1. kinematic rows 至少要有 1 个 movable joint。
2. row name 非空、不重复。
3. type 必须是 Revolute/Prismatic/FixedFrame/ToolFrame 之一。
4. ToolFrame 最多 1 个。
5. limits 只能引用 movable joints。
6. 每个 movable joint 推荐有且只有 1 条 limit；第一阶段可允许缺失，但重复必须报错。
7. 每个 pose.q.size() 必须等于 movable joint 数量。
8. dynamics.forceLimits 只能引用 movable joints。
9. drawables/dynamics.links 可引用 Base、movable joints、FixedFrame、ToolFrame。
```

错误信息建议:

```cpp
errors << QString ("Kinematics row %1 has unsupported type '%2'.")
              .arg (rowIndex + 1)
              .arg (QString::fromStdString (row.type));
errors << "At most one ToolFrame/TCP row is allowed.";
errors << QString ("Limit references non-movable frame %1.").arg (...);
errors << QString ("Pose %1 has %2 values, expected %3 active joints.")
              .arg (...);
```

- [ ] **Step 6: 更新 `<Q>` 输出**

在 `makeSerialDeviceXml()` 中先得到 active rows:

```cpp
const std::vector< JointTransformSpec > activeRows = activeJointRows (spec);
```

输出 pose:

```cpp
for (const PoseSpec& pose : spec.poses) {
    out << "  <Q name=\"" << QString::fromStdString (pose.name) << "\">";
    for (size_t i = 0; i < activeRows.size (); ++i) {
        if (i > 0)
            out << " ";
        const KinematicRowType type = kinematicRowTypeFromString (activeRows[i].type);
        out << number (qValueForXml (pose.q[i], type));
    }
    out << "</Q>\n";
}
```

- [ ] **Step 7: 运行测试**

Run 同 Step 2。

Expected: Prismatic 测试通过，现有默认 6 Revolute 测试仍通过。

- [ ] **Step 8: Commit**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "支持移动关节的单位和Q向量序列化"
```

---

### Task 3: 输出混合 Joint/Frame 链并处理 TCP

**Files:**
- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelXmlWriter.cpp`
- Test: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: 写 FixedFrame 测试**

添加:

```cpp
{
    RobotModelSpec fixed = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    fixed.mode = RobotModelMode::JointRPYPos;
    fixed.transformJoints[2].name = "ElbowBracket";
    fixed.transformJoints[2].type = "FixedFrame";
    fixed.transformJoints[2].rpyDeg = {{0, 0, 45}};
    fixed.transformJoints[2].pos = {{0.1, 0.2, 0.3}};
    fixed.limits.erase (fixed.limits.begin () + 2);
    fixed.poses[0].q = {0, 0, 0, 0, 0};
    fixed.poses[1].q = {0, -90, 90, 0, 0};

    QStringList errors;
    if (!RobotModelXmlWriter::validate (fixed, errors))
        return fail ("FixedFrame model should validate: " + errors.join ("; "));

    const QString xml = RobotModelXmlWriter::makeSerialDeviceXml (fixed);
    if (contains (xml, "<Joint name=\"ElbowBracket\""))
        return fail ("FixedFrame should not serialize as Joint.");
    if (!contains (xml, "<Frame name=\"ElbowBracket\""))
        return fail ("FixedFrame should serialize as Frame.");
    if (contains (xml, "refjoint=\"ElbowBracket\""))
        return fail ("FixedFrame should not have limits.");
    if (!contains (xml, "<Q name=\"Zero\">0 0 0 0 0</Q>"))
        return fail ("FixedFrame should not contribute to Q dimension.");
}
```

- [ ] **Step 2: 写 ToolFrame 测试**

添加:

```cpp
{
    RobotModelSpec tool = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    tool.mode = RobotModelMode::JointRPYPos;

    JointTransformSpec tcp;
    tcp.name = "TCP";
    tcp.type = "ToolFrame";
    tcp.rpyDeg = {{0, 0, 0}};
    tcp.pos = {{0, 0, 0.18}};
    tool.transformJoints.push_back (tcp);

    QStringList errors;
    if (!RobotModelXmlWriter::validate (tool, errors))
        return fail ("Explicit ToolFrame should validate: " + errors.join ("; "));

    const QString xml = RobotModelXmlWriter::makeSerialDeviceXml (tool);
    if (xml.count ("<Frame name=\"TCP\"") != 1)
        return fail ("Explicit ToolFrame should replace the auto-appended TCP frame.");
    if (!contains (xml, "<Frame name=\"TCP\" refframe=\"Joint6\""))
        return fail ("ToolFrame TCP should reference previous kinematic row.");

    tool.transformJoints.push_back (tcp);
    errors.clear ();
    if (RobotModelXmlWriter::validate (tool, errors))
        return fail ("Duplicate ToolFrame rows should fail validation.");
}
```

- [ ] **Step 3: 运行测试确认失败**

Expected: 当前 writer 会把 FixedFrame/ToolFrame 输出成 `<Joint type="...">`，或重复自动 TCP。

- [ ] **Step 4: 实现混合链输出**

在 `makeSerialDeviceXml()` 中将非纯 Revolute DH 模式和 JointRPYPos 模式统一成:

```cpp
const bool useNativeDh = hasOnlyRevoluteDhRows (spec);
if (useNativeDh) {
    // 保留现有 <DHJoint type="schilling"> 输出
}
else {
    QString previous = "Base";
    bool hasExplicitToolFrame = false;
    for (const JointTransformSpec& row : effectiveKinematicRows (spec)) {
        const KinematicRowType rowType = kinematicRowTypeFromString (row.type);
        if (isMovableJointType (rowType)) {
            out << "  <Joint name=\"" << QString::fromStdString (row.name) << "\" type=\""
                << QString::fromLatin1 (kinematicRowTypeName (rowType)) << "\">\n";
            out << "    <RPY>" << vector3 (row.rpyDeg) << "</RPY>\n";
            out << "    <Pos>" << vector3 (row.pos) << "</Pos>\n";
            if (spec.showFrameAxes)
                out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
            out << "  </Joint>\n";
            previous = QString::fromStdString (row.name);
        }
        else if (isFrameRowType (rowType)) {
            if (rowType == KinematicRowType::ToolFrame)
                hasExplicitToolFrame = true;
            out << "  <Frame name=\"" << QString::fromStdString (row.name)
                << "\" refframe=\"" << previous << "\"";
            if (rowType == KinematicRowType::FixedFrame)
                out << " type=\"Fixed\"";
            out << ">\n";
            out << "    <RPY>" << vector3 (row.rpyDeg) << "</RPY>\n";
            out << "    <Pos>" << vector3 (row.pos) << "</Pos>\n";
            if (spec.showFrameAxes)
                out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
            out << "  </Frame>\n";
            previous = QString::fromStdString (row.name);
        }
    }
    if (!hasExplicitToolFrame) {
        out << "  <Frame name=\"TCP\" refframe=\"" << previous << "\">\n";
        out << "    <RPY>0 0 0</RPY>\n";
        out << "    <Pos>0 0 0</Pos>\n";
        if (spec.showFrameAxes)
            out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
        out << "  </Frame>\n";
    }
}
```

注意: 如果 RobWork XML reader 对 SerialDevice 内 `<Frame refframe="...">` 的链式语义有额外限制，先保留字符串测试，再增加实际 XML load 验证。不要把 `FixedFrame` 写成 `<Joint type="FixedFrame">`，RobWork 没有这种关节类型。

- [ ] **Step 5: 更新自动 TCP 逻辑**

删除原先无条件根据最后一个 joint 追加 TCP 的代码，替换为:

```text
1. useNativeDh=true 时保持旧行为: TCP refframe = 最后一个 DHJoint。
2. useNativeDh=false 时由混合链输出块负责:
   - 有 ToolFrame 行: 不再自动追加 TCP。
   - 无 ToolFrame 行: 自动追加 name="TCP" 的零位工具帧。
```

- [ ] **Step 6: 运行测试**

Expected: FixedFrame/ToolFrame 新测试通过，纯默认 DH 旧测试仍看到 6 个 `<DHJoint>` 和一个 TCP。

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "支持固定帧和工具帧的XML输出"
```

---

### Task 4: DH 与 Transform 同步保留类型并明确 Prismatic 参数

**Files:**
- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelXmlWriter.hpp`
- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelXmlWriter.cpp`
- Test: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: 写 Prismatic DH 同步测试**

添加:

```cpp
{
    DHJointSpec dh;
    dh.name = "Slide";
    dh.type = "Prismatic";
    dh.alphaDeg = 90;
    dh.a = 0.2;
    dh.d = 0.05;
    dh.offsetDeg = 30;

    const JointTransformSpec joint = RobotModelXmlWriter::dhJointToTransform (dh);
    if (joint.type != "Prismatic")
        return fail ("dhJointToTransform should preserve DH type Prismatic.");
    if (!near (joint.pos[0], 0.2 * std::cos (30.0 * RobotModelXmlWriter::kPi / 180.0)) ||
        !near (joint.pos[1], 0.2 * std::sin (30.0 * RobotModelXmlWriter::kPi / 180.0)) ||
        !near (joint.pos[2], 0.05))
        return fail ("Prismatic DH fixed transform should still use a/offset/d projection.");

    bool lossy = false;
    const DHJointSpec back = RobotModelXmlWriter::transformJointToDh (joint, &lossy);
    if (back.type != "Prismatic")
        return fail ("transformJointToDh should preserve Prismatic type.");
    if (lossy)
        return fail ("Prismatic DH round-trip should be lossless for pitch=0 consistent input.");
}
```

- [ ] **Step 2: 更新转换实现**

`dhJointToTransform()`:

```cpp
const std::string type = existingType.empty () ? dh.type : existingType;
joint.type = type.empty () ? "Revolute" : std::string (kinematicRowTypeName (kinematicRowTypeFromString (type)));
```

`transformJointToDh()`:

```cpp
dh.type = std::string (kinematicRowTypeName (kinematicRowTypeFromString (joint.type)));
if (dh.type == "Unknown")
    dh.type = joint.type;
```

- [ ] **Step 3: 更新同步函数**

`syncTransformJointsFromDh()` 保留 DH 行类型优先:

```cpp
const std::string existingType = spec.dhJoints[i].type.empty ()
    ? spec.transformJoints[i].type
    : spec.dhJoints[i].type;
spec.transformJoints[i] = dhJointToTransform (spec.dhJoints[i], existingType);
```

`syncDhJointsFromTransform()` 把 Transform 类型写回 DH:

```cpp
DHJointSpec dh = transformJointToDh (spec.transformJoints[i]);
dh.type = spec.transformJoints[i].type;
spec.dhJoints[i] = dh;
```

- [ ] **Step 4: 运行测试**

Expected: 同步测试通过；已有 `dhJointToTransform 保留 Prismatic` 测试继续通过。

- [ ] **Step 5: Commit**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "保持DH和RPY建模中的关节类型同步"
```

---

### Task 5: UI 支持类型选择、动态行数和 DH 参数提示

**Files:**
- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelBuilderWidget.hpp`
- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelBuilderWidget.cpp`
- Test: build target `sdurws_robotmodelbuilder`

- [ ] **Step 1: 修改表头**

Kinematics Transform 表头改为:

```cpp
QStringList () << "Name"
               << "Type"
               << "RPY deg (Z Y X)"
               << "Pos m"
```

DH 表头改为:

```cpp
QStringList () << "Name"
               << "Type"
               << "alpha deg"
               << "a m"
               << "d m"
               << "offset deg"
               << "Active variable"
```

Limits 表头改为:

```cpp
QStringList () << "Joint"
               << "Type"
               << "PosMin deg/m"
               << "PosMax deg/m"
               << "VelMax deg/s or m/s"
               << "AccMax deg/s2 or m/s2"
```

- [ ] **Step 2: 新增 Kinematics 行按钮**

在 Kinematics tab 表格下方增加:

```cpp
QPushButton* addKinRowBtn = new QPushButton ("Add Row");
QPushButton* delKinRowBtn = new QPushButton ("Remove Row");
```

连接到新 slot:

```cpp
connect (addKinRowBtn, SIGNAL (clicked ()), this, SLOT (addKinematicRow ()));
connect (delKinRowBtn, SIGNAL (clicked ()), this, SLOT (removeSelectedKinematicRow ()));
```

`RobotModelBuilderWidget.hpp` 增加:

```cpp
void addKinematicRow ();
void removeSelectedKinematicRow ();
void updateDependentTablesFromKinematics ();
```

- [ ] **Step 3: 实现类型受限选择**

最小实现可以先用可编辑文本加 validate；推荐实现本地 delegate:

```cpp
class JointTypeDelegate : public QStyledItemDelegate
{
  public:
    explicit JointTypeDelegate (QObject* parent = NULL) : QStyledItemDelegate (parent) {}

    QWidget* createEditor (QWidget* parent, const QStyleOptionViewItem&,
                           const QModelIndex&) const override
    {
        QComboBox* combo = new QComboBox (parent);
        combo->addItems (QStringList () << "Revolute" << "Prismatic" << "FixedFrame" << "ToolFrame");
        return combo;
    }

    void setEditorData (QWidget* editor, const QModelIndex& index) const override
    {
        QComboBox* combo = qobject_cast< QComboBox* > (editor);
        if (combo == NULL)
            return;
        const QString value = index.data (Qt::EditRole).toString ();
        const int i = combo->findText (value);
        combo->setCurrentIndex (i >= 0 ? i : 0);
    }

    void setModelData (QWidget* editor, QAbstractItemModel* model,
                       const QModelIndex& index) const override
    {
        QComboBox* combo = qobject_cast< QComboBox* > (editor);
        if (combo != NULL)
            model->setData (index, combo->currentText (), Qt::EditRole);
    }
};
```

在 `buildUi()` 中:

```cpp
JointTypeDelegate* typeDelegate = new JointTypeDelegate (this);
_transformTable->setItemDelegateForColumn (1, typeDelegate);
_dhTable->setItemDelegateForColumn (1, typeDelegate);
```

如果当前 Qt 版本没有需要的 include，加入:

```cpp
#include <QStyledItemDelegate>
#include <QAbstractItemModel>
```

- [ ] **Step 4: 实现 DH Active variable 文本**

增加 helper:

```cpp
static QString activeVariableText (const QString& type)
{
    const KinematicRowType rowType = kinematicRowTypeFromString (type.toStdString ());
    switch (rowType) {
        case KinematicRowType::Revolute: return "theta=q+offset";
        case KinematicRowType::Prismatic: return "d=q+d0";
        case KinematicRowType::FixedFrame: return "none";
        case KinematicRowType::ToolFrame: return "tool";
        default: return "unknown";
    }
}
```

在 `fillKinematicsTables()` 和两个 table changed slot 中同步第 6 列:

```cpp
setItem (_dhTable, row, 6, activeVariableText (QString::fromStdString (dh.type)), false);
```

- [ ] **Step 5: 更新 collect/fill 列索引**

DH collect:

```cpp
joint.name = itemText (_dhTable, row, 0).toStdString ();
joint.type = itemText (_dhTable, row, 1).toStdString ();
joint.alphaDeg = itemDouble (_dhTable, row, 2);
joint.a = itemDouble (_dhTable, row, 3);
joint.d = itemDouble (_dhTable, row, 4);
joint.offsetDeg = itemDouble (_dhTable, row, 5);
```

Transform collect 保持 Type 在第 1 列。

Limits collect:

```cpp
limit.jointName = itemText (_limitsTable, row, 0).toStdString ();
limit.posMin = itemDouble (_limitsTable, row, 2);
limit.posMax = itemDouble (_limitsTable, row, 3);
limit.velMax = itemDouble (_limitsTable, row, 4);
limit.accMax = itemDouble (_limitsTable, row, 5);
```

- [ ] **Step 6: 动态更新 Limits 和 Poses**

实现 `updateDependentTablesFromKinematics()`:

```text
1. 从当前 Transform 表收集所有 row name/type。
2. 过滤 Revolute/Prismatic。
3. Limits 表行数设为 active joint 数量。
4. Limits 表第 0 列 joint name，第 1 列 type，type 列不可编辑。
5. Poses 表列数设为 1 + active joint 数量。
6. Poses 表第 0 列为 Name。
7. Poses 表头对 Revolute 用 "Joint1 deg"，对 Prismatic 用 "Slide m"。
8. 如果原 pose 中已有旧值，按列顺序保留能保留的值；新增列填 0。
```

该函数在以下时机调用:

```text
1. addKinematicRow/removeSelectedKinematicRow 后。
2. Transform 或 DH 表的 Name/Type 修改后。
3. fillFromSpec() 完成 Kinematics 回填后。
```

- [ ] **Step 7: 行增删实现**

`addKinematicRow()` 默认追加一个 FixedFrame 或 Revolute。建议默认 Revolute，名称为 `JointN`:

```cpp
const int row = _transformTable->rowCount ();
_transformTable->insertRow (row);
_dhTable->insertRow (row);
setItem (_transformTable, row, 0, "Joint" + QString::number (row + 1));
setItem (_transformTable, row, 1, "Revolute");
setItem (_transformTable, row, 2, "0 0 0");
setItem (_transformTable, row, 3, "0 0 0");
setItem (_dhTable, row, 0, "Joint" + QString::number (row + 1));
setItem (_dhTable, row, 1, "Revolute");
setItem (_dhTable, row, 2, "0");
setItem (_dhTable, row, 3, "0");
setItem (_dhTable, row, 4, "0");
setItem (_dhTable, row, 5, "0");
setItem (_dhTable, row, 6, "theta=q+offset", false);
updateDependentTablesFromKinematics ();
```

`removeSelectedKinematicRow()` 根据当前可见表的 currentRow 删除两张表同一行，并确保至少保留 1 个 movable joint。若删除会导致无 active joint，拒绝并 `setStatus("At least one movable joint is required.")`。

- [ ] **Step 8: 手工验证 UI**

Build:

```powershell
cmd /s /c "call ""D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake --build ""D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release"" --target sdurws_robotmodelbuilder --config Release"
```

Manual expected:

```text
1. Transform/DH 两张表都能把某行 Type 改为 Prismatic。
2. DH 表 Active variable 显示 d=q+d0。
3. Limits 表该行单位表头包含 deg/m，Type 列显示 Prismatic。
4. Poses 表该列 header 显示该关节名和 m。
5. 将某行改为 FixedFrame 后，Limits 和 Poses 中对应列消失。
6. 添加 ToolFrame 后，XML preview 只有一个 TCP/tool frame，不额外追加重复 TCP。
```

- [ ] **Step 9: Commit**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp
git commit -m "完善关节类型选择和动态参数表"
```

---

### Task 6: 自动连杆几何支持非 6 行和固定帧

**Files:**
- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelXmlWriter.cpp`
- Test: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: 写变长行测试**

添加:

```cpp
{
    RobotModelSpec mixed = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    mixed.mode = RobotModelMode::JointRPYPos;
    JointTransformSpec fixedFrame;
    fixedFrame.name = "CameraMount";
    fixedFrame.type = "FixedFrame";
    fixedFrame.rpyDeg = {{0, 0, 0}};
    fixedFrame.pos = {{0.05, 0, 0.1}};
    mixed.transformJoints.insert (mixed.transformJoints.begin () + 3, fixedFrame);
    mixed.dhJoints.insert (mixed.dhJoints.begin () + 3,
                           RobotModelXmlWriter::transformJointToDh (fixedFrame));

    std::array< double, 3 > pos;
    std::array< double, 3 > rpy;
    double length = 0;
    RobotModelXmlWriter::computeLinkPose (mixed, 2, pos, rpy, length);
    if (!(length > 0))
        return fail ("computeLinkPose should support mixed variable-length kinematic rows.");
}
```

- [ ] **Step 2: 更新 computeLinkPose**

将 `JointCount` 限制替换为 `effectiveKinematicRows(spec).size()`:

```cpp
const std::vector< JointTransformSpec > rows = effectiveKinematicRows (spec);
if (linkIndex < 0 || static_cast< size_t > (linkIndex + 1) >= rows.size ()) {
    posOut = {{0, 0, 0}};
    rpyDegOut = {{0, 0, 0}};
    lengthOut = 0;
    return;
}
const std::array< double, 3 >& delta = rows[linkIndex + 1].pos;
```

如果 link 几何希望只连接 movable joints，改为在 `appendLinks()` 中只生成相邻有效 frame 的 `LinkAtoB`。第一阶段建议保持“相邻运动学行之间生成 Link”，因为 FixedFrame/ToolFrame 也有可视坐标位置。

- [ ] **Step 3: 更新 appendLinks/applyLinkGeometry**

把固定 `JointCount - 1` 的循环改为基于 `effectiveKinematicRows(spec).size() - 1`。生成 drawable 名称时避免依赖 `Joint1To2` 数字:

```text
默认 6 轴仍生成 Link1To2...Link5To6 以保持旧测试。
如果出现自定义行名，新增 drawable 命名为 Link_<from>_To_<to>，并将非法文件/XML 字符替换为下划线。
```

若不想改变现有默认命名，新增 helper:

```cpp
QString autoLinkName (const std::vector< JointTransformSpec >& rows, size_t i)
{
    if (rows[i].name == "Joint" + std::to_string (i + 1) &&
        rows[i + 1].name == "Joint" + std::to_string (i + 2))
        return "Link" + QString::number (i + 1) + "To" + QString::number (i + 2);
    return "Link_" + sanitizeFrameName (...) + "_To_" + sanitizeFrameName (...);
}
```

- [ ] **Step 4: 运行测试**

Expected: 旧连杆方向测试和新混合行测试都通过。

- [ ] **Step 5: Commit**

```bash
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "让自动连杆几何适配混合运动学行"
```

---

### Task 7: 文档和用户说明

**Files:**
- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\docs\RobotModelBuilder.md`
- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\docs\RobotModelBuilderDynamics.md` if dynamics force units are mentioned there

- [ ] **Step 1: 更新数据模型章节**

在 `RobotModelBuilder.md` 的 `JointTransformSpec` 和 `DHJointSpec` 章节加入:

```markdown
### 关节/帧类型

| Type | XML | Q向量 | 限位单位 | 说明 |
| --- | --- | --- | --- | --- |
| Revolute | `<Joint type="Revolute">` 或纯 DH `<DHJoint>` | 是 | deg | 绕局部 Z 轴旋转 |
| Prismatic | `<Joint type="Prismatic">` | 是 | m | 沿局部 Z 轴平移 |
| FixedFrame | `<Frame type="Fixed">` | 否 | 无 | 固定中间坐标系 |
| ToolFrame | `<Frame name="TCP">` | 否 | 无 | 末端工具/TCP |
```

- [ ] **Step 2: 增加 DH 参数意义表**

加入本计划“DH 表参数含义”中的表格，并明确:

```markdown
Prismatic 使用 DH 表时，`d m` 表示零位平移偏置 `d0`，实际关节变量为 `q`，RobWork 中沿关节局部 Z 轴平移，等价于 `d = d0 + q`。`offset deg` 在 Prismatic 中不是运动变量，而是固定 `theta`。
```

- [ ] **Step 3: 增加 XML 示例**

添加混合链示例:

```xml
<Joint name="Joint1" type="Revolute">
  <RPY>0 0 0</RPY>
  <Pos>0 0 0.35</Pos>
</Joint>
<Joint name="Slide2" type="Prismatic">
  <RPY>0 0 90</RPY>
  <Pos>0.12 0 0</Pos>
</Joint>
<Frame name="CameraMount" refframe="Slide2" type="Fixed">
  <RPY>0 0 0</RPY>
  <Pos>0.05 0 0.1</Pos>
</Frame>
<Frame name="TCP" refframe="Joint6">
  <RPY>0 0 0</RPY>
  <Pos>0 0 0.18</Pos>
</Frame>
<PosLimit refjoint="Slide2" min="0" max="0.4" />
<Q name="Home">0 0.125 0 0 0 0</Q>
```

- [ ] **Step 4: 更新测试说明**

记录测试命令:

```powershell
cmd /s /c "call ""D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake --build ""D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release"" --target sdurws_robotmodelbuilder_xmltest --config Release"
```

- [ ] **Step 5: Commit**

```bash
git add docs/RobotModelBuilder.md docs/RobotModelBuilderDynamics.md
git commit -m "更新机器人建模插件关节类型文档"
```

---

### Task 8: 最终验证和回归检查

**Files:**
- Inspect: all modified files

- [ ] **Step 1: 检查工作区变更**

Run:

```powershell
git -c safe.directory=D:/10_Source_Repos/21_robot/RobWork/RobWork status --short
```

Expected: 只包含本功能相关文件。

- [ ] **Step 2: 构建 XML 测试目标**

Run:

```powershell
cmd /s /c "call ""D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake --build ""D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release"" --target sdurws_robotmodelbuilder_xmltest --config Release"
```

Expected: 编译和链接成功。

- [ ] **Step 3: 运行 XML 测试可执行文件**

先定位:

```powershell
Get-ChildItem -LiteralPath "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release" -Recurse -Filter "sdurws_robotmodelbuilder_xmltest.exe" | Select-Object -First 1 -ExpandProperty FullName
```

然后运行定位到的 exe。Expected: 正常情况下 exit code 0。如果仍出现本机已知的 exit code 1 且无 stdout/stderr，必须记录“测试目标已编译，运行阶段疑似本机 DLL/运行时环境问题”，不能声称测试通过。

- [ ] **Step 4: 构建插件目标**

Run:

```powershell
cmd /s /c "call ""D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake --build ""D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release"" --target sdurws_robotmodelbuilder --config Release"
```

Expected: 插件目标编译链接成功。

- [ ] **Step 5: 手动 XML preview 验证**

在 RobWorkStudio 插件界面执行:

```text
1. 默认模型直接 Generate Preview。
   预期: 仍是 6 个 Revolute，纯 DH 模式仍可输出 6 个 DHJoint。
2. 将 Joint2 改为 Prismatic，limit 设置 0..0.4，pose q2 设置 0.125。
   预期: XML 中 Joint2 为 Prismatic，limit 是米，Q 的第二项是 0.125。
3. 添加 FixedFrame CameraMount。
   预期: XML 中 CameraMount 是 Frame，不出现在 Q 和 limit 中。
4. 添加 ToolFrame TCP，Pos 设置 0 0 0.18。
   预期: XML 中只有一个 TCP，refframe 是前一个运动学行。
5. 将 Prismatic 行切换到 DH 表。
   预期: Active variable 显示 d=q+d0，d m 作为零位偏置，offset deg 作为固定 theta。
```

- [ ] **Step 6: 最终提交**

如果前面每个 task 已单独提交，这一步只提交遗漏文档或测试修正:

```bash
git status --short
git add <remaining-files>
git commit -m "完善机器人建模插件混合关节类型支持"
```

---

## 自检清单

- [ ] `Revolute` 仍兼容默认 6 轴机器人和现有 DH 输出。
- [ ] `Prismatic` 的 limit 和 pose 不被当作角度转弧度。
- [ ] `Prismatic` 的 DH 表明确 `d` 是变量偏置，`offset` 是固定角。
- [ ] `FixedFrame` 不进入 Q，不需要 limit，不输出成 Joint。
- [ ] `ToolFrame/TCP` 最多一个；显式 ToolFrame 存在时不再自动追加第二个 TCP。
- [ ] `PoseSpec::q` 长度等于 movable joints 数量，而不是运动学行数。
- [ ] `Drawable` 和 dynamics link 可引用 FixedFrame/ToolFrame。
- [ ] `ForceLimit` 只引用 Revolute/Prismatic，单位说明为 Revolute Nm、Prismatic N。
- [ ] UI 表格不再硬编码所有地方都是 6 行；默认仍创建 6 个 Revolute。
- [ ] 新旧回归测试都能编译，运行失败时明确区分环境问题和断言失败。
