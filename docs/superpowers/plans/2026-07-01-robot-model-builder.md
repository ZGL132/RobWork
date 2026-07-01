# RobotModelBuilder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a new RobWorkStudio plugin named `RobotModelBuilder` that graphically generates six-axis robot XML files and can save/load the generated scene.

**Architecture:** Add an internal `robotmodelbuilder` plugin under `RobWorkStudio/src/rwslibs`. Keep XML generation and validation in small UI-independent C++ files, then build the Qt widget and plugin shell around those files.

**Tech Stack:** C++17-compatible code style used by RobWork, Qt Widgets, RobWorkStudio plugin API, CMake, RobWork XML conventions.

---

## File Structure

- Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`
  - Owns enums and plain structs for kinematics, drawables, limits, poses, and full robot specification.
- Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`
  - Declares validation, default model creation, XML generation, file name helpers, and save helpers.
- Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
  - Implements model validation, degree-to-radian conversion for poses, serial device XML, scene XML, and file writing.
- Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
  - Declares the Qt widget and its slots.
- Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
  - Implements the form UI, tables, model collection, preview, save, and save-and-load actions.
- Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp`
  - Declares the RobWorkStudio plugin entry point.
- Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp`
  - Installs the widget and connects save-and-load to RobWorkStudio.
- Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`
  - Builds `sdurws_robotmodelbuilder` and a small XML writer test executable.
- Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/plugin.json`
  - Plugin metadata.
- Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/resources.qrc`
  - Minimal Qt resource file.
- Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`
  - Small self-contained executable using return codes and string checks.
- Modify `RobWorkStudio/src/rwslibs/CMakeLists.txt`
  - Add `add_subdirectory(robotmodelbuilder)`.

## Task 1: Add Data Model and XML Writer

**Files:**
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`

- [ ] **Step 1: Create the plugin directory**

Run:

```powershell
New-Item -ItemType Directory -Force -Path 'D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder'
```

Expected: directory exists.

- [ ] **Step 2: Add `RobotModelSpec.hpp`**

Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp`:

```cpp
#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELSPEC_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELSPEC_HPP

#include <array>
#include <string>
#include <vector>

namespace rws {

enum class RobotModelMode
{
    DH,
    JointRPYPos
};

struct DHJointSpec
{
    std::string name;
    double alphaDeg;
    double a;
    double d;
    double offsetDeg;
};

struct JointTransformSpec
{
    std::string name;
    std::string type;
    std::array< double, 3 > rpyDeg;
    std::array< double, 3 > pos;
};

struct DrawableSpec
{
    std::string name;
    std::string refFrame;
    std::string shape;
    double radius;
    double length;
    std::array< double, 3 > rpyDeg;
    std::array< double, 3 > pos;
    std::array< double, 3 > rgb;
    bool collisionModel;
};

struct JointLimitSpec
{
    std::string jointName;
    double posMinDeg;
    double posMaxDeg;
    double velMaxDeg;
    double accMaxDeg;
};

struct PoseSpec
{
    std::string name;
    std::array< double, 6 > qDeg;
};

struct RobotModelSpec
{
    std::string robotName;
    std::string saveDirectory;
    RobotModelMode mode;
    bool showFrameAxes;
    bool generateDrawables;
    bool generateScene;
    std::vector< DHJointSpec > dhJoints;
    std::vector< JointTransformSpec > transformJoints;
    std::vector< DrawableSpec > drawables;
    std::vector< JointLimitSpec > limits;
    std::vector< PoseSpec > poses;
};

}    // namespace rws

#endif
```

- [ ] **Step 3: Add `RobotModelXmlWriter.hpp`**

Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`:

```cpp
#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELXMLWRITER_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELXMLWRITER_HPP

#include "RobotModelSpec.hpp"

#include <QString>
#include <QStringList>

namespace rws {

class RobotModelXmlWriter
{
  public:
    static RobotModelSpec makeDefaultSixAxisModel (const QString& saveDirectory);
    static QString sanitizeFileBaseName (const QString& name);
    static bool validate (const RobotModelSpec& spec, QStringList& errors);
    static QString makeSerialDeviceXml (const RobotModelSpec& spec);
    static QString makeSceneXml (const RobotModelSpec& spec);
    static QString serialDeviceFilePath (const RobotModelSpec& spec);
    static QString sceneFilePath (const RobotModelSpec& spec);
    static bool saveFiles (const RobotModelSpec& spec, QStringList& errors);

  private:
    static QString number (double value);
    static QString vector3 (const std::array< double, 3 >& values);
    static double degToRad (double value);
};

}    // namespace rws

#endif
```

- [ ] **Step 4: Add `RobotModelXmlWriter.cpp`**

Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp` with:

```cpp
#include "RobotModelXmlWriter.hpp"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <cmath>
#include <set>

using namespace rws;

namespace {
const int JointCount = 6;

bool isEmpty (const std::string& value)
{
    return QString::fromStdString (value).trimmed ().isEmpty ();
}

void appendJointAxes (RobotModelSpec& spec)
{
    for (int i = 0; i < JointCount; ++i) {
        DrawableSpec drawable;
        drawable.name           = "Joint" + std::to_string (i + 1) + "Axis";
        drawable.refFrame       = "Joint" + std::to_string (i + 1);
        drawable.shape          = "Cylinder";
        drawable.radius         = 0.105 - 0.01 * i;
        drawable.length         = 0.18 - 0.012 * i;
        drawable.rpyDeg         = {{0, 0, 0}};
        drawable.pos            = {{0, 0, 0}};
        drawable.rgb            = {{0.6, 0.6, 0.6}};
        drawable.collisionModel = false;
        spec.drawables.push_back (drawable);
    }
}

void appendLinks (RobotModelSpec& spec)
{
    const double lengths[5] = {0.12, 0.52, 0.42, 0.38, 0.12};
    const double radii[5]   = {0.055, 0.05, 0.045, 0.04, 0.035};
    for (int i = 0; i < 5; ++i) {
        DrawableSpec drawable;
        drawable.name           = "Link" + std::to_string (i + 1) + "To" + std::to_string (i + 2);
        drawable.refFrame       = "Joint" + std::to_string (i + 1);
        drawable.shape          = "Cylinder";
        drawable.radius         = radii[i];
        drawable.length         = lengths[i];
        drawable.rpyDeg         = i < 3 ? std::array< double, 3 > {{0, 90, 0}} :
                                           std::array< double, 3 > {{0, 0, 0}};
        drawable.pos            = i < 3 ? std::array< double, 3 > {{lengths[i] / 2.0, 0, 0}} :
                                           std::array< double, 3 > {{0, 0, lengths[i] / 2.0}};
        drawable.rgb            = {{0.35, 0.45, 0.65}};
        drawable.collisionModel = false;
        spec.drawables.push_back (drawable);
    }
}
}    // namespace

RobotModelSpec RobotModelXmlWriter::makeDefaultSixAxisModel (const QString& saveDirectory)
{
    RobotModelSpec spec;
    spec.robotName         = "GenericSixAxis";
    spec.saveDirectory     = saveDirectory.toStdString ();
    spec.mode              = RobotModelMode::JointRPYPos;
    spec.showFrameAxes     = true;
    spec.generateDrawables = true;
    spec.generateScene     = true;

    const double rpy[JointCount][3] = {{0, 0, 0}, {0, 90, 0}, {0, 0, 0},
                                       {0, 90, 0}, {0, -90, 0}, {0, 90, 0}};
    const double pos[JointCount][3] = {{0, 0, 0.35}, {0.12, 0, 0}, {0.52, 0, 0},
                                       {0.42, 0, 0}, {0, 0, 0.38}, {0, 0, 0.12}};
    for (int i = 0; i < JointCount; ++i) {
        DHJointSpec dh;
        dh.name      = "Joint" + std::to_string (i + 1);
        dh.alphaDeg  = rpy[i][1];
        dh.a         = pos[i][0];
        dh.d         = pos[i][2];
        dh.offsetDeg = 0;
        spec.dhJoints.push_back (dh);

        JointTransformSpec joint;
        joint.name   = dh.name;
        joint.type   = "Revolute";
        joint.rpyDeg = {{rpy[i][0], rpy[i][1], rpy[i][2]}};
        joint.pos    = {{pos[i][0], pos[i][1], pos[i][2]}};
        spec.transformJoints.push_back (joint);
    }

    appendJointAxes (spec);
    appendLinks (spec);

    const double posMin[JointCount] = {-180, -120, -150, -180, -120, -360};
    const double posMax[JointCount] = {180, 120, 150, 180, 120, 360};
    const double vel[JointCount]    = {120, 120, 120, 180, 180, 240};
    const double acc[JointCount]    = {360, 360, 360, 540, 540, 720};
    for (int i = 0; i < JointCount; ++i) {
        JointLimitSpec limit;
        limit.jointName = "Joint" + std::to_string (i + 1);
        limit.posMinDeg = posMin[i];
        limit.posMaxDeg = posMax[i];
        limit.velMaxDeg = vel[i];
        limit.accMaxDeg = acc[i];
        spec.limits.push_back (limit);
    }

    PoseSpec zero;
    zero.name = "Zero";
    zero.qDeg = {{0, 0, 0, 0, 0, 0}};
    spec.poses.push_back (zero);

    PoseSpec home;
    home.name = "Home";
    home.qDeg = {{0, -90, 90, 0, 0, 0}};
    spec.poses.push_back (home);

    return spec;
}

QString RobotModelXmlWriter::sanitizeFileBaseName (const QString& name)
{
    QString result = name.trimmed ();
    result.replace (QRegularExpression ("[^A-Za-z0-9_\\-]"), "_");
    return result;
}

bool RobotModelXmlWriter::validate (const RobotModelSpec& spec, QStringList& errors)
{
    errors.clear ();
    const QString robotName = QString::fromStdString (spec.robotName).trimmed ();
    if (robotName.isEmpty ())
        errors << "Robot name is required.";
    if (sanitizeFileBaseName (robotName).isEmpty ())
        errors << "Robot name must contain at least one safe file-name character.";
    if (!QDir (QString::fromStdString (spec.saveDirectory)).exists ())
        errors << "Save directory does not exist.";

    const std::vector< std::string > activeJoints =
        spec.mode == RobotModelMode::DH ?
            [&] {
                std::vector< std::string > names;
                for (const DHJointSpec& joint : spec.dhJoints)
                    names.push_back (joint.name);
                return names;
            }() :
            [&] {
                std::vector< std::string > names;
                for (const JointTransformSpec& joint : spec.transformJoints)
                    names.push_back (joint.name);
                return names;
            }();

    if (activeJoints.size () != JointCount)
        errors << "Exactly six joints are required.";

    std::set< std::string > jointNames;
    for (const std::string& name : activeJoints) {
        if (isEmpty (name))
            errors << "Joint names must not be empty.";
        if (!jointNames.insert (name).second)
            errors << QString ("Duplicate joint name: %1").arg (QString::fromStdString (name));
    }

    for (const DrawableSpec& drawable : spec.drawables) {
        if (isEmpty (drawable.name))
            errors << "Drawable names must not be empty.";
        if (isEmpty (drawable.refFrame))
            errors << QString ("Drawable %1 requires a reference frame.")
                          .arg (QString::fromStdString (drawable.name));
        if (drawable.radius <= 0)
            errors << QString ("Drawable %1 radius must be greater than zero.")
                          .arg (QString::fromStdString (drawable.name));
        if (drawable.length <= 0)
            errors << QString ("Drawable %1 length must be greater than zero.")
                          .arg (QString::fromStdString (drawable.name));
        for (double color : drawable.rgb) {
            if (color < 0 || color > 1)
                errors << QString ("Drawable %1 RGB values must be between 0 and 1.")
                              .arg (QString::fromStdString (drawable.name));
        }
    }

    for (const JointLimitSpec& limit : spec.limits) {
        if (isEmpty (limit.jointName))
            errors << "Limit rows require a joint name.";
        if (limit.posMinDeg >= limit.posMaxDeg)
            errors << QString ("Position min must be less than max for %1.")
                          .arg (QString::fromStdString (limit.jointName));
        if (limit.velMaxDeg <= 0)
            errors << QString ("Velocity limit must be greater than zero for %1.")
                          .arg (QString::fromStdString (limit.jointName));
        if (limit.accMaxDeg <= 0)
            errors << QString ("Acceleration limit must be greater than zero for %1.")
                          .arg (QString::fromStdString (limit.jointName));
    }

    for (const PoseSpec& pose : spec.poses) {
        if (isEmpty (pose.name))
            errors << "Pose names must not be empty.";
    }

    return errors.isEmpty ();
}

QString RobotModelXmlWriter::makeSerialDeviceXml (const RobotModelSpec& spec)
{
    QString xml;
    QTextStream out (&xml);
    out << "<SerialDevice name=\"" << QString::fromStdString (spec.robotName) << "\">\n";

    if (spec.mode == RobotModelMode::DH) {
        for (const DHJointSpec& joint : spec.dhJoints) {
            out << "  <DHJoint name=\"" << QString::fromStdString (joint.name) << "\" alpha=\""
                << number (joint.alphaDeg) << "\" a=\"" << number (joint.a) << "\" d=\""
                << number (joint.d) << "\" offset=\"" << number (joint.offsetDeg)
                << "\" type=\"schilling\" />\n";
        }
    }
    else {
        for (const JointTransformSpec& joint : spec.transformJoints) {
            out << "  <Joint name=\"" << QString::fromStdString (joint.name) << "\" type=\""
                << QString::fromStdString (joint.type) << "\">\n";
            out << "    <RPY>" << vector3 (joint.rpyDeg) << "</RPY>\n";
            out << "    <Pos>" << vector3 (joint.pos) << "</Pos>\n";
            if (spec.showFrameAxes)
                out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
            out << "  </Joint>\n";
        }
    }

    if (spec.generateDrawables) {
        for (const DrawableSpec& drawable : spec.drawables) {
            out << "  <Drawable name=\"" << QString::fromStdString (drawable.name)
                << "\" refframe=\"" << QString::fromStdString (drawable.refFrame) << "\"";
            if (drawable.collisionModel)
                out << " colmodel=\"Enabled\"";
            out << ">\n";
            out << "    <RPY>" << vector3 (drawable.rpyDeg) << "</RPY>\n";
            out << "    <Pos>" << vector3 (drawable.pos) << "</Pos>\n";
            out << "    <RGB>" << vector3 (drawable.rgb) << "</RGB>\n";
            out << "    <Cylinder radius=\"" << number (drawable.radius) << "\" z=\""
                << number (drawable.length) << "\" />\n";
            out << "  </Drawable>\n";
        }
    }

    for (const JointLimitSpec& limit : spec.limits) {
        out << "  <PosLimit refjoint=\"" << QString::fromStdString (limit.jointName)
            << "\" min=\"" << number (limit.posMinDeg) << "\" max=\"" << number (limit.posMaxDeg)
            << "\" />\n";
    }
    for (const JointLimitSpec& limit : spec.limits) {
        out << "  <VelLimit refjoint=\"" << QString::fromStdString (limit.jointName)
            << "\" max=\"" << number (limit.velMaxDeg) << "\" />\n";
    }
    for (const JointLimitSpec& limit : spec.limits) {
        out << "  <AccLimit refjoint=\"" << QString::fromStdString (limit.jointName)
            << "\" max=\"" << number (limit.accMaxDeg) << "\" />\n";
    }

    for (const PoseSpec& pose : spec.poses) {
        out << "  <Q name=\"" << QString::fromStdString (pose.name) << "\">";
        for (int i = 0; i < JointCount; ++i) {
            if (i > 0)
                out << " ";
            out << number (degToRad (pose.qDeg[i]));
        }
        out << "</Q>\n";
    }

    out << "</SerialDevice>\n";
    return xml;
}

QString RobotModelXmlWriter::makeSceneXml (const RobotModelSpec& spec)
{
    const QString robotName = sanitizeFileBaseName (QString::fromStdString (spec.robotName));
    QString xml;
    QTextStream out (&xml);
    out << "<WorkCell name=\"" << robotName << "Scene\">\n";
    out << "  <Frame name=\"RobotBase\" refframe=\"WORLD\">\n";
    out << "    <RPY>0 0 0</RPY>\n";
    out << "    <Pos>0 0 0</Pos>\n";
    if (spec.showFrameAxes)
        out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
    out << "  </Frame>\n\n";
    out << "  <Include file=\"" << robotName << ".wc.xml\" />\n";
    out << "</WorkCell>\n";
    return xml;
}

QString RobotModelXmlWriter::serialDeviceFilePath (const RobotModelSpec& spec)
{
    QDir dir (QString::fromStdString (spec.saveDirectory));
    return dir.filePath (sanitizeFileBaseName (QString::fromStdString (spec.robotName)) + ".wc.xml");
}

QString RobotModelXmlWriter::sceneFilePath (const RobotModelSpec& spec)
{
    QDir dir (QString::fromStdString (spec.saveDirectory));
    return dir.filePath (sanitizeFileBaseName (QString::fromStdString (spec.robotName)) +
                         "Scene.wc.xml");
}

bool RobotModelXmlWriter::saveFiles (const RobotModelSpec& spec, QStringList& errors)
{
    if (!validate (spec, errors))
        return false;

    QFile deviceFile (serialDeviceFilePath (spec));
    if (!deviceFile.open (QFile::WriteOnly | QFile::Text)) {
        errors << QString ("Could not write %1").arg (deviceFile.fileName ());
        return false;
    }
    QTextStream deviceStream (&deviceFile);
    deviceStream << makeSerialDeviceXml (spec);
    deviceFile.close ();

    if (spec.generateScene) {
        QFile sceneFile (sceneFilePath (spec));
        if (!sceneFile.open (QFile::WriteOnly | QFile::Text)) {
            errors << QString ("Could not write %1").arg (sceneFile.fileName ());
            return false;
        }
        QTextStream sceneStream (&sceneFile);
        sceneStream << makeSceneXml (spec);
        sceneFile.close ();
    }

    return true;
}

QString RobotModelXmlWriter::number (double value)
{
    return QString::number (value, 'g', 15);
}

QString RobotModelXmlWriter::vector3 (const std::array< double, 3 >& values)
{
    return number (values[0]) + " " + number (values[1]) + " " + number (values[2]);
}

double RobotModelXmlWriter::degToRad (double value)
{
    return value * 3.14159265358979323846 / 180.0;
}
```

- [ ] **Step 5: Commit Task 1**

Run:

```powershell
git add -- RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpec.hpp RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp
git commit -m "Add RobotModelBuilder XML model writer"
```

Expected: commit succeeds.

## Task 2: Add XML Writer Smoke Test

**Files:**
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`

- [ ] **Step 1: Add `RobotModelXmlWriterTest.cpp`**

Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`:

```cpp
#include "RobotModelXmlWriter.hpp"

#include <QCoreApplication>
#include <QDir>
#include <iostream>

using namespace rws;

namespace {
int fail (const QString& message)
{
    std::cerr << message.toStdString () << std::endl;
    return 1;
}

bool contains (const QString& text, const QString& needle)
{
    return text.contains (needle);
}
}    // namespace

int main (int argc, char** argv)
{
    QCoreApplication app (argc, argv);
    RobotModelSpec spec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());

    QStringList errors;
    if (!RobotModelXmlWriter::validate (spec, errors))
        return fail ("Default model did not validate: " + errors.join ("; "));

    const QString serialXml = RobotModelXmlWriter::makeSerialDeviceXml (spec);
    const QString sceneXml  = RobotModelXmlWriter::makeSceneXml (spec);

    if (!contains (serialXml, "<SerialDevice name=\"GenericSixAxis\">"))
        return fail ("Serial XML missing SerialDevice root.");
    if (serialXml.count ("<Joint name=\"") != 6)
        return fail ("Serial XML must contain six explicit Joint elements.");
    if (!contains (serialXml, "<Drawable name=\"Joint1Axis\""))
        return fail ("Serial XML missing default joint drawable.");
    if (!contains (serialXml, "<PosLimit refjoint=\"Joint1\""))
        return fail ("Serial XML missing position limit.");
    if (!contains (serialXml, "<Q name=\"Home\">0 -1.5707963267949 1.5707963267949 0 0 0</Q>"))
        return fail ("Home pose was not converted to radians.");
    if (!contains (sceneXml, "<Include file=\"GenericSixAxis.wc.xml\" />"))
        return fail ("Scene XML missing include.");

    spec.transformJoints[1].name = spec.transformJoints[0].name;
    if (RobotModelXmlWriter::validate (spec, errors))
        return fail ("Duplicate joint name should fail validation.");

    spec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    spec.drawables[0].radius = 0;
    if (RobotModelXmlWriter::validate (spec, errors))
        return fail ("Zero drawable radius should fail validation.");

    return 0;
}
```

- [ ] **Step 2: Add initial `CMakeLists.txt`**

Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`:

```cmake
set(SUBSYS_NAME sdurws_robotmodelbuilder)
set(SUBSYS_DESC "Robot model builder plugin")
set(SUBSYS_DEPS
    PUBLIC  sdurws
            RW::sdurw_core
            RW::sdurw_models
            RW::sdurw_loaders
)

set(build TRUE)
rw_subsys_option(build ${SUBSYS_NAME} ${SUBSYS_DESC} ON DEPENDS ${SUBSYS_DEPS} ADD_DOC)

if(build)
    set(ModelSrcFiles RobotModelXmlWriter.cpp)
    set(ModelHeaderFiles RobotModelSpec.hpp RobotModelXmlWriter.hpp)

    add_executable(sdurws_robotmodelbuilder_xmltest
        RobotModelXmlWriterTest.cpp
        ${ModelSrcFiles}
        ${ModelHeaderFiles}
    )
    target_link_libraries(sdurws_robotmodelbuilder_xmltest PRIVATE ${QT_LIBRARIES})
    target_include_directories(sdurws_robotmodelbuilder_xmltest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
```

- [ ] **Step 3: Temporarily wire the directory into `rwslibs`**

Modify `RobWorkStudio/src/rwslibs/CMakeLists.txt` and add:

```cmake
add_subdirectory(robotmodelbuilder)
```

Place it after `add_subdirectory(workcelleditorplugin)` and before `add_subdirectory(rwstudioapp)`.

- [ ] **Step 4: Configure and build the smoke test**

Run the command matching the user's existing build directory. If using the known Qt Creator Release build, run:

```powershell
cmake --build D:\10_Source_Repos\21_robot\RobWork\Build\RWS --config Release --target sdurws_robotmodelbuilder_xmltest -- /m
```

If the build directory differs, locate it in Qt Creator's build settings and run the same target name.

Expected: target builds successfully.

- [ ] **Step 5: Run the smoke test**

Run:

```powershell
D:\10_Source_Repos\21_robot\RobWork\Build\RWS\bin\Release\sdurws_robotmodelbuilder_xmltest.exe
```

Expected: exit code `0` and no stderr output.

- [ ] **Step 6: Commit Task 2**

Run:

```powershell
git add -- RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt RobWork/RobWorkStudio/src/rwslibs/CMakeLists.txt
git commit -m "Test RobotModelBuilder XML writer"
```

Expected: commit succeeds.

## Task 3: Add Plugin Shell

**Files:**
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp`
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp`
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/plugin.json`
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/resources.qrc`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`

- [ ] **Step 1: Add `plugin.json`**

Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/plugin.json`:

```json
{
            "name" : "RobotModelBuilder",
         "version" : "1.0.0",
    "dependencies" : []
}
```

- [ ] **Step 2: Add `resources.qrc`**

Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/resources.qrc`:

```xml
<!DOCTYPE RCC><RCC version="1.0">
<qresource>
</qresource>
</RCC>
```

- [ ] **Step 3: Add `RobotModelBuilderPlugin.hpp`**

Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp`:

```cpp
#ifndef RWS_ROBOTMODELBUILDER_PLUGIN_HPP
#define RWS_ROBOTMODELBUILDER_PLUGIN_HPP

#include <rws/RobWorkStudioPlugin.hpp>

namespace rws {

class RobotModelBuilderWidget;

class RobotModelBuilderPlugin : public RobWorkStudioPlugin
{
    Q_OBJECT
#ifndef RWS_USE_STATIC_LINK_PLUGINS
    Q_INTERFACES (rws::RobWorkStudioPlugin)
    Q_PLUGIN_METADATA (IID "dk.sdu.mip.Robwork.RobWorkStudioPlugin/0.1" FILE "plugin.json")
#endif
  public:
    RobotModelBuilderPlugin ();
    virtual ~RobotModelBuilderPlugin ();

    void initialize ();
    void open (rw::models::WorkCell* workcell);
    void close ();

  private Q_SLOTS:
    void loadSceneFile (const QString& filename);

  private:
    RobotModelBuilderWidget* _widget;
};

}    // namespace rws

#endif
```

- [ ] **Step 4: Add temporary `RobotModelBuilderPlugin.cpp`**

Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp`:

```cpp
#include "RobotModelBuilderPlugin.hpp"

#include <rws/RobWorkStudio.hpp>

#include <QLabel>
#include <QVBoxLayout>

using namespace rws;

RobotModelBuilderPlugin::RobotModelBuilderPlugin () :
    RobWorkStudioPlugin ("RobotModelBuilder", QIcon ()), _widget (NULL)
{}

RobotModelBuilderPlugin::~RobotModelBuilderPlugin ()
{}

void RobotModelBuilderPlugin::initialize ()
{
    QWidget* widget  = new QWidget (this);
    QVBoxLayout* lay = new QVBoxLayout (widget);
    lay->addWidget (new QLabel ("RobotModelBuilder"));
    widget->setLayout (lay);
    setWidget (widget);
}

void RobotModelBuilderPlugin::open (rw::models::WorkCell* workcell)
{}

void RobotModelBuilderPlugin::close ()
{}

void RobotModelBuilderPlugin::loadSceneFile (const QString& filename)
{
    if (getRobWorkStudio () != NULL)
        getRobWorkStudio ()->setWorkcell (filename.toStdString ());
}
```

- [ ] **Step 5: Update `CMakeLists.txt` to build plugin shell**

Replace `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt` with:

```cmake
set(SUBSYS_NAME sdurws_robotmodelbuilder)
set(SUBSYS_DESC "Robot model builder plugin")
set(SUBSYS_DEPS
    PUBLIC  sdurws
            RW::sdurw_core
            RW::sdurw_models
            RW::sdurw_loaders
)

set(build TRUE)
rw_subsys_option(build ${SUBSYS_NAME} ${SUBSYS_DESC} ON DEPENDS ${SUBSYS_DEPS} ADD_DOC)

if(build)
    set(ModelSrcFiles RobotModelXmlWriter.cpp)
    set(ModelHeaderFiles RobotModelSpec.hpp RobotModelXmlWriter.hpp)
    set(SrcFiles
        RobotModelBuilderPlugin.cpp
        ${ModelSrcFiles}
    )
    set(SRC_FILES_HPP
        RobotModelBuilderPlugin.hpp
        ${ModelHeaderFiles}
    )

    if(DEFINED Qt6Core_VERSION)
        qt_add_resources(RccSrcFiles resources.qrc)
    else()
        qt5_add_resources(RccSrcFiles resources.qrc)
    endif()

    rws_add_plugin(
        ${SUBSYS_NAME}
        ${RWS_DEFAULT_LIB_TYPE}
        ${SrcFiles}
        ${MocSrcFiles}
        ${RccSrcFiles}
    )
    rw_add_includes(${SUBSYS_NAME} "rwslibs/robotmodelbuilder" ${SRC_FILES_HPP})
    target_link_libraries(${SUBSYS_NAME} ${SUBSYS_DEPS} PUBLIC ${QT_LIBRARIES})
    target_include_directories(${SUBSYS_NAME}
        INTERFACE
        $<BUILD_INTERFACE:${RWS_ROOT}/src> $<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>
    )

    rws_plugin_load_details(${SUBSYS_NAME} 2 RobotModelBuilder false)
    if("${RWS_DEFAULT_LIB_TYPE}" STREQUAL "STATIC")
        set(RWS_PLUGIN_LIBRARIES ${RWS_PLUGIN_LIBRARIES} ${SUBSYS_NAME} PARENT_SCOPE)
    endif()

    add_executable(sdurws_robotmodelbuilder_xmltest
        RobotModelXmlWriterTest.cpp
        ${ModelSrcFiles}
        ${ModelHeaderFiles}
    )
    target_link_libraries(sdurws_robotmodelbuilder_xmltest PRIVATE ${QT_LIBRARIES})
    target_include_directories(sdurws_robotmodelbuilder_xmltest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
```

- [ ] **Step 6: Build plugin shell**

Run:

```powershell
cmake --build D:\10_Source_Repos\21_robot\RobWork\Build\RWS --config Release --target sdurws_robotmodelbuilder -- /m
```

Expected: plugin target builds successfully.

- [ ] **Step 7: Commit Task 3**

Run:

```powershell
git add -- RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/plugin.json RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/resources.qrc RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt
git commit -m "Add RobotModelBuilder plugin shell"
```

Expected: commit succeeds.

## Task 4: Add Qt Widget UI

**Files:**
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`

- [ ] **Step 1: Add `RobotModelBuilderWidget.hpp`**

Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`:

```cpp
#ifndef RWS_ROBOTMODELBUILDER_WIDGET_HPP
#define RWS_ROBOTMODELBUILDER_WIDGET_HPP

#include "RobotModelSpec.hpp"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextEdit;

namespace rws {

class RobotModelBuilderWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit RobotModelBuilderWidget (QWidget* parent = NULL);

  Q_SIGNALS:
    void loadSceneRequested (const QString& filename);

  private Q_SLOTS:
    void resetDefaults ();
    void generatePreview ();
    void saveXml ();
    void saveAndLoad ();
    void browseSaveDirectory ();
    void modeChanged (int index);

  private:
    void buildUi ();
    void fillFromSpec (const RobotModelSpec& spec);
    RobotModelSpec collectSpec () const;
    void fillKinematicsTables (const RobotModelSpec& spec);
    void fillDrawablesTable (const RobotModelSpec& spec);
    void fillLimitsTable (const RobotModelSpec& spec);
    void fillPosesTable (const RobotModelSpec& spec);
    void showErrors (const QStringList& errors);
    void setStatus (const QString& message);
    static QString itemText (const QTableWidget* table, int row, int column);
    static double itemDouble (const QTableWidget* table, int row, int column);
    static void setItem (QTableWidget* table, int row, int column, const QString& value);

  private:
    QLineEdit* _robotName;
    QLineEdit* _saveDirectory;
    QComboBox* _mode;
    QCheckBox* _showFrameAxes;
    QCheckBox* _generateDrawables;
    QCheckBox* _generateScene;
    QTableWidget* _dhTable;
    QTableWidget* _transformTable;
    QTableWidget* _drawablesTable;
    QTableWidget* _limitsTable;
    QTableWidget* _posesTable;
    QTextEdit* _serialPreview;
    QTextEdit* _scenePreview;
    QLineEdit* _status;
};

}    // namespace rws

#endif
```

- [ ] **Step 2: Add `RobotModelBuilderWidget.cpp`**

Create `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`.
The implementation should:

- Construct the UI with `QVBoxLayout`, `QFormLayout`, `QTabWidget`, `QTableWidget`, and `QTextEdit`.
- Use two kinematics tables and show one at a time based on mode.
- Use `RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::homePath())` in `resetDefaults`.
- Use `RobotModelXmlWriter::validate`, `makeSerialDeviceXml`, and `makeSceneXml` in `generatePreview`.
- Use `RobotModelXmlWriter::saveFiles` in `saveXml`.
- Emit `loadSceneRequested(RobotModelXmlWriter::sceneFilePath(spec))` after a successful save in `saveAndLoad`.

Use this key slot logic:

```cpp
void RobotModelBuilderWidget::generatePreview ()
{
    RobotModelSpec spec = collectSpec ();
    QStringList errors;
    if (!RobotModelXmlWriter::validate (spec, errors)) {
        showErrors (errors);
        return;
    }
    _serialPreview->setPlainText (RobotModelXmlWriter::makeSerialDeviceXml (spec));
    _scenePreview->setPlainText (RobotModelXmlWriter::makeSceneXml (spec));
    setStatus ("Preview generated.");
}

void RobotModelBuilderWidget::saveXml ()
{
    RobotModelSpec spec = collectSpec ();
    QStringList errors;
    if (!RobotModelXmlWriter::saveFiles (spec, errors)) {
        showErrors (errors);
        return;
    }
    generatePreview ();
    setStatus ("XML files saved.");
}

void RobotModelBuilderWidget::saveAndLoad ()
{
    RobotModelSpec spec = collectSpec ();
    QStringList errors;
    if (!RobotModelXmlWriter::saveFiles (spec, errors)) {
        showErrors (errors);
        return;
    }
    generatePreview ();
    Q_EMIT loadSceneRequested (RobotModelXmlWriter::sceneFilePath (spec));
    setStatus ("XML files saved. Loading scene...");
}
```

- [ ] **Step 3: Replace temporary plugin body**

Modify `RobotModelBuilderPlugin.cpp` to use the widget:

```cpp
#include "RobotModelBuilderPlugin.hpp"

#include "RobotModelBuilderWidget.hpp"

#include <rws/RobWorkStudio.hpp>

using namespace rws;

RobotModelBuilderPlugin::RobotModelBuilderPlugin () :
    RobWorkStudioPlugin ("RobotModelBuilder", QIcon ()), _widget (NULL)
{}

RobotModelBuilderPlugin::~RobotModelBuilderPlugin ()
{}

void RobotModelBuilderPlugin::initialize ()
{
    _widget = new RobotModelBuilderWidget (this);
    connect (_widget, SIGNAL (loadSceneRequested (const QString&)), this,
             SLOT (loadSceneFile (const QString&)));
    setWidget (_widget);
}

void RobotModelBuilderPlugin::open (rw::models::WorkCell* workcell)
{}

void RobotModelBuilderPlugin::close ()
{}

void RobotModelBuilderPlugin::loadSceneFile (const QString& filename)
{
    if (getRobWorkStudio () != NULL)
        getRobWorkStudio ()->setWorkcell (filename.toStdString ());
}
```

- [ ] **Step 4: Update `CMakeLists.txt` for widget files**

Add `RobotModelBuilderWidget.cpp` to `SrcFiles` and `RobotModelBuilderWidget.hpp` to `SRC_FILES_HPP`.

- [ ] **Step 5: Build plugin**

Run:

```powershell
cmake --build D:\10_Source_Repos\21_robot\RobWork\Build\RWS --config Release --target sdurws_robotmodelbuilder -- /m
```

Expected: plugin target builds successfully.

- [ ] **Step 6: Commit Task 4**

Run:

```powershell
git add -- RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt
git commit -m "Add RobotModelBuilder Qt interface"
```

Expected: commit succeeds.

## Task 5: Final Integration and Verification

**Files:**
- Modify as needed only if build or runtime verification finds concrete issues.

- [ ] **Step 1: Build XML test**

Run:

```powershell
cmake --build D:\10_Source_Repos\21_robot\RobWork\Build\RWS --config Release --target sdurws_robotmodelbuilder_xmltest -- /m
```

Expected: target builds successfully.

- [ ] **Step 2: Run XML test**

Run:

```powershell
D:\10_Source_Repos\21_robot\RobWork\Build\RWS\bin\Release\sdurws_robotmodelbuilder_xmltest.exe
```

Expected: exit code `0`.

- [ ] **Step 3: Build RobWorkStudio**

Run:

```powershell
cmake --build D:\10_Source_Repos\21_robot\RobWork\Build\RWS --config Release --target RobWorkStudio -- /m
```

Expected: RobWorkStudio builds successfully.

- [ ] **Step 4: Run RobWorkStudio from Qt Creator or command line**

Use the same PATH setup already used for the user's working Release build, then run:

```powershell
D:\10_Source_Repos\21_robot\RobWork\Build\RWS\bin\Release\RobWorkStudio.exe
```

Expected:

- RobWorkStudio starts.
- `RobotModelBuilder` is available as a plugin panel.
- Reset defaults and generate preview works.
- Save XML creates `GenericSixAxis.wc.xml` and `GenericSixAxisScene.wc.xml`.
- Save and Load opens the generated scene.

- [ ] **Step 5: Validate generated files with RobWorkStudio loader**

In the plugin, click `Save and Load`.

Expected:

- No error dialog.
- RobWorkStudio 3D view displays the simple cylindrical robot.

- [ ] **Step 6: Commit any integration fixes**

If any concrete fix was needed, run:

```powershell
git add -- RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder RobWork/RobWorkStudio/src/rwslibs/CMakeLists.txt
git commit -m "Fix RobotModelBuilder integration"
```

Expected: commit succeeds only when files changed.

## Self-Review

Spec coverage:

- Independent plugin: covered by Tasks 3 and 4.
- Six-axis model: covered by Tasks 1, 2, and 4.
- DH and `Joint + RPY + Pos` modes: covered by Tasks 1 and 4.
- Drawable parameters: covered by Tasks 1 and 4.
- Limits and poses: covered by Tasks 1, 2, and 4.
- XML preview, save, and save-and-load: covered by Task 4.
- Build and runtime verification: covered by Task 5.

Placeholder scan:

- No placeholder markers or undefined future work remains in the plan.

Type consistency:

- `RobotModelSpec`, `RobotModelXmlWriter`, `RobotModelBuilderWidget`, and `RobotModelBuilderPlugin` names are consistent across tasks.
