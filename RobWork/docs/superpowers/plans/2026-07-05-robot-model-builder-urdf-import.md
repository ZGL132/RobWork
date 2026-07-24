# RobotModelBuilder URDF Import Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add URDF import to the existing RobotModelBuilder plugin so a designed `.urdf` file can be imported, inspected, edited, saved as RobWork XML, and used for later simulation analysis.

**Architecture:** Implement URDF import as a focused model-layer module inside `RobWorkStudio/src/rwslibs/robotmodelbuilder`, not as a separate plugin. The importer parses URDF into the existing `RobotModelSpec`; the existing widget then displays it through `fillFromSpec()`, and the existing writer saves `.wc.xml`, scene XML, collision setup XML, proximity setup XML, and `.dwc.xml`.

**Tech Stack:** C++11-style code matching this repository, QtCore XML APIs (`QXmlStreamReader`), QtWidgets for the import button/dialog, existing `RobotModelSpec`, `RobotModelXmlWriter`, `sdurws_robotmodelbuilder_xmltest`, CMake.

---

## Important Design Rules

- Do not create a new RobWorkStudio plugin for this feature. Add a new importer module to the existing `sdurws_robotmodelbuilder` plugin.
- Keep `RobotModelSpec::transformJoints` as the single source of truth. URDF is an input format, not a parallel model.
- Keep DH as a read-only projection. After import, call `RobotModelXmlWriter::refreshDhProjectionFromTransform(spec)`.
- URDF `origin rpy` is radians in X-Y-Z semantic order: roll around X, pitch around Y, yaw around Z. The current plugin UI/writer stores RobWork RPY values in degrees with the existing internal order used by `RobotModelXmlWriter`. Convert explicitly and test it.
- The first implementation supports a single serial chain plus fixed frames. If a URDF has branches, import one deterministic root-to-tip chain, warn about skipped branches, and keep the warning visible in the UI.
- Do not add a ROS dependency. `package://` paths are resolved with a user-provided package root list and a best-effort fallback.

## Files

- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.hpp`
  - Public importer API and warning/result data types.
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp`
  - URDF parsing, graph construction, conversion to `RobotModelSpec`, path resolution.
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
  - Add `importUrdf()` slot.
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
  - Add `Import URDF` button, file dialog, importer call, warning display.
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`
  - Add importer files to plugin and test target.
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`
  - Add importer tests to the existing command-line regression executable.
- Modify: `docs/RobotModelBuilder.md`
  - Document the URDF import behavior, limitations, and path handling.

## Test Commands

Use these from repository root `D:\10_Source_Repos\21_robot\RobWork\RobWork`:

```powershell
cmake --build build --target sdurws_robotmodelbuilder_xmltest --config Debug
```

Find and run the executable if the build layout is unknown:

```powershell
Get-ChildItem -LiteralPath build -Recurse -Filter 'sdurws_robotmodelbuilder_xmltest*.exe' | Select-Object -First 1 -ExpandProperty FullName
```

Then run the returned executable path, for example:

```powershell
.\build\bin\Debug\sdurws_robotmodelbuilder_xmltest.exe
```

Expected final result:

```text
All RobotModelBuilder XML tests passed.
```

---

### Task 1: Add Importer API and CMake Wiring

**Files:**
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.hpp`
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`
- Test: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Create the importer header**

Add `RobotModelUrdfImporter.hpp`:

```cpp
#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELURDFIMPORTER_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELURDFIMPORTER_HPP

#include "RobotModelSpec.hpp"

#include <QString>
#include <QStringList>

namespace rws {

struct UrdfImportOptions
{
    QString saveDirectory;
    QStringList packageRoots;
    bool generateScene = true;
    bool generateDrawables = true;
    bool generateDynamicWorkCell = true;
};

struct UrdfImportResult
{
    RobotModelSpec spec;
    QStringList warnings;
};

class RobotModelUrdfImporter
{
  public:
    static bool importFile (const QString& urdfPath,
                            const UrdfImportOptions& options,
                            UrdfImportResult& result,
                            QStringList& errors);
};

}    // namespace rws

#endif
```

- [ ] **Step 2: Create a minimal implementation that reports an unsupported import**

Add `RobotModelUrdfImporter.cpp`:

```cpp
#include "RobotModelUrdfImporter.hpp"

using namespace rws;

bool RobotModelUrdfImporter::importFile (const QString& urdfPath,
                                         const UrdfImportOptions& options,
                                         UrdfImportResult& result,
                                         QStringList& errors)
{
    Q_UNUSED (urdfPath);
    Q_UNUSED (options);
    result = UrdfImportResult ();
    errors << "URDF import parser has not been implemented yet.";
    return false;
}
```

- [ ] **Step 3: Wire the importer into CMake**

Modify `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`:

```cmake
set(ModelSrcFiles
    RobotModelXmlWriter.cpp
    RobotModelUrdfImporter.cpp
)
set(ModelHeaderFiles
    RobotModelSpec.hpp
    RobotModelXmlWriter.hpp
    RobotModelUrdfImporter.hpp
)
```

Keep the rest of the file structure intact so both `sdurws_robotmodelbuilder` and `sdurws_robotmodelbuilder_xmltest` compile the importer.

- [ ] **Step 4: Add a compile-only importer call to the test executable**

Near the top of `RobotModelXmlWriterTest.cpp`, add:

```cpp
#include "RobotModelUrdfImporter.hpp"
```

Inside `main`, after `QCoreApplication app(argc, argv);`, add:

```cpp
    {
        UrdfImportOptions importOptions;
        importOptions.saveDirectory = QDir::tempPath ();
        UrdfImportResult importResult;
        QStringList importErrors;
        if (RobotModelUrdfImporter::importFile ("missing.urdf", importOptions, importResult,
                                                importErrors)) {
            return fail ("Unimplemented URDF importer should not report success.");
        }
        if (importErrors.isEmpty ())
            return fail ("Unimplemented URDF importer should report a clear error.");
    }
```

- [ ] **Step 5: Build and run the test**

Run:

```powershell
cmake --build build --target sdurws_robotmodelbuilder_xmltest --config Debug
```

Expected: build succeeds.

Run the test executable. Expected: existing test suite still passes.

- [ ] **Step 6: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "feat: add URDF importer scaffold"
```

---

### Task 2: Parse URDF Robot, Links, Joints, Origins, Axes, and Limits

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add a failing minimal URDF parse test**

Replace the compile-only test from Task 1 with this block inside `main`:

```cpp
    {
        const QString dir = QDir::tempPath () + "/robotmodelbuilder_urdf_minimal";
        QDir ().mkpath (dir);
        const QString urdfPath = dir + "/minimal.urdf";
        QFile file (urdfPath);
        if (!file.open (QFile::WriteOnly | QFile::Text))
            return fail ("Could not create minimal URDF test file.");
        QTextStream out (&file);
        out << "<robot name=\"MiniBot\">\n"
            << "  <link name=\"base_link\" />\n"
            << "  <link name=\"link1\" />\n"
            << "  <joint name=\"joint1\" type=\"revolute\">\n"
            << "    <parent link=\"base_link\" />\n"
            << "    <child link=\"link1\" />\n"
            << "    <origin xyz=\"0.1 0.2 0.3\" rpy=\"0.4 0.5 0.6\" />\n"
            << "    <axis xyz=\"0 0 1\" />\n"
            << "    <limit lower=\"-1.57\" upper=\"1.57\" velocity=\"2.5\" effort=\"9.0\" />\n"
            << "  </joint>\n"
            << "</robot>\n";
        file.close ();

        UrdfImportOptions options;
        options.saveDirectory = dir;
        UrdfImportResult result;
        QStringList importErrors;
        if (!RobotModelUrdfImporter::importFile (urdfPath, options, result, importErrors))
            return fail ("Minimal URDF import failed: " + importErrors.join ("; "));
        if (result.spec.robotName != "MiniBot")
            return fail ("URDF robot name was not imported.");
        if (result.spec.transformJoints.size () != 1)
            return fail ("Minimal URDF should import one transform joint.");
        const JointTransformSpec& joint = result.spec.transformJoints.front ();
        if (joint.name != "joint1")
            return fail ("URDF joint name was not imported.");
        if (joint.type != "Revolute")
            return fail ("URDF revolute joint type was not converted to Revolute.");
        if (!near (joint.pos[0], 0.1) || !near (joint.pos[1], 0.2) ||
            !near (joint.pos[2], 0.3))
            return fail ("URDF origin xyz was not imported.");
        if (result.spec.limits.size () != 1)
            return fail ("URDF joint limit was not imported.");
        if (!near (result.spec.limits[0].posMin, -1.57 * 180.0 / RobotModelXmlWriter::kPi) ||
            !near (result.spec.limits[0].posMax, 1.57 * 180.0 / RobotModelXmlWriter::kPi))
            return fail ("URDF revolute limits should be converted from radians to degrees.");
        if (result.spec.dynamics.forceLimits.size () != 1 ||
            !near (result.spec.dynamics.forceLimits[0].maxForce, 9.0))
            return fail ("URDF effort limit was not imported.");
    }
```

- [ ] **Step 2: Add private parser data structures in the `.cpp` file**

Inside `RobotModelUrdfImporter.cpp`, add an anonymous namespace:

```cpp
#include "RobotModelXmlWriter.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include <algorithm>
#include <map>
#include <set>

namespace {

struct UrdfLink
{
    QString name;
};

struct UrdfOrigin
{
    std::array< double, 3 > xyz = {{0, 0, 0}};
    std::array< double, 3 > rpyRad = {{0, 0, 0}};
};

struct UrdfLimit
{
    bool hasLower = false;
    bool hasUpper = false;
    bool hasVelocity = false;
    bool hasEffort = false;
    double lower = 0;
    double upper = 0;
    double velocity = 0;
    double effort = 0;
};

struct UrdfJoint
{
    QString name;
    QString type;
    QString parentLink;
    QString childLink;
    UrdfOrigin origin;
    std::array< double, 3 > axis = {{0, 0, 1}};
    UrdfLimit limit;
};

struct UrdfModel
{
    QString robotName;
    std::map< QString, UrdfLink > links;
    std::vector< UrdfJoint > joints;
};

bool parseVector3 (const QString& text, std::array< double, 3 >& values)
{
    const QStringList parts = text.split (QRegularExpression ("\\s+"), Qt::SkipEmptyParts);
    if (parts.size () != 3)
        return false;
    for (int i = 0; i < 3; ++i) {
        bool ok = false;
        values[i] = parts[i].toDouble (&ok);
        if (!ok)
            return false;
    }
    return true;
}

double radToDeg (double value)
{
    return value * 180.0 / RobotModelXmlWriter::kPi;
}

std::array< double, 3 > urdfRpyToPluginRpyDeg (const std::array< double, 3 >& rpyRad)
{
    return {{radToDeg (rpyRad[2]), radToDeg (rpyRad[1]), radToDeg (rpyRad[0])}};
}

QString jointTypeToPluginType (const QString& type)
{
    if (type.compare ("revolute", Qt::CaseInsensitive) == 0 ||
        type.compare ("continuous", Qt::CaseInsensitive) == 0)
        return "Revolute";
    if (type.compare ("prismatic", Qt::CaseInsensitive) == 0)
        return "Prismatic";
    if (type.compare ("fixed", Qt::CaseInsensitive) == 0)
        return "FixedFrame";
    return "FixedFrame";
}

}    // namespace
```

- [ ] **Step 3: Implement XML parsing**

Add helper functions in the anonymous namespace:

```cpp
bool parseOrigin (QXmlStreamReader& xml, UrdfOrigin& origin, QStringList& errors)
{
    const QXmlStreamAttributes attrs = xml.attributes ();
    if (attrs.hasAttribute ("xyz") &&
        !parseVector3 (attrs.value ("xyz").toString (), origin.xyz)) {
        errors << "Invalid URDF origin xyz value.";
        return false;
    }
    if (attrs.hasAttribute ("rpy") &&
        !parseVector3 (attrs.value ("rpy").toString (), origin.rpyRad)) {
        errors << "Invalid URDF origin rpy value.";
        return false;
    }
    return true;
}

bool parseJointElement (QXmlStreamReader& xml, UrdfJoint& joint, QStringList& errors)
{
    joint.name = xml.attributes ().value ("name").toString ();
    joint.type = xml.attributes ().value ("type").toString ();
    if (joint.name.trimmed ().isEmpty ()) {
        errors << "URDF joint without name is not supported.";
        return false;
    }
    while (xml.readNextStartElement ()) {
        const QString tag = xml.name ().toString ();
        if (tag == "parent") {
            joint.parentLink = xml.attributes ().value ("link").toString ();
            xml.skipCurrentElement ();
        }
        else if (tag == "child") {
            joint.childLink = xml.attributes ().value ("link").toString ();
            xml.skipCurrentElement ();
        }
        else if (tag == "origin") {
            if (!parseOrigin (xml, joint.origin, errors))
                return false;
            xml.skipCurrentElement ();
        }
        else if (tag == "axis") {
            if (!parseVector3 (xml.attributes ().value ("xyz").toString (), joint.axis)) {
                errors << QString ("Invalid axis on URDF joint %1.").arg (joint.name);
                return false;
            }
            xml.skipCurrentElement ();
        }
        else if (tag == "limit") {
            const QXmlStreamAttributes attrs = xml.attributes ();
            bool ok = false;
            if (attrs.hasAttribute ("lower")) {
                joint.limit.lower = attrs.value ("lower").toDouble (&ok);
                if (!ok) { errors << QString ("Invalid lower limit on %1.").arg (joint.name); return false; }
                joint.limit.hasLower = true;
            }
            if (attrs.hasAttribute ("upper")) {
                joint.limit.upper = attrs.value ("upper").toDouble (&ok);
                if (!ok) { errors << QString ("Invalid upper limit on %1.").arg (joint.name); return false; }
                joint.limit.hasUpper = true;
            }
            if (attrs.hasAttribute ("velocity")) {
                joint.limit.velocity = attrs.value ("velocity").toDouble (&ok);
                if (!ok) { errors << QString ("Invalid velocity limit on %1.").arg (joint.name); return false; }
                joint.limit.hasVelocity = true;
            }
            if (attrs.hasAttribute ("effort")) {
                joint.limit.effort = attrs.value ("effort").toDouble (&ok);
                if (!ok) { errors << QString ("Invalid effort limit on %1.").arg (joint.name); return false; }
                joint.limit.hasEffort = true;
            }
            xml.skipCurrentElement ();
        }
        else {
            xml.skipCurrentElement ();
        }
    }
    if (joint.parentLink.trimmed ().isEmpty () || joint.childLink.trimmed ().isEmpty ()) {
        errors << QString ("URDF joint %1 must have parent and child links.").arg (joint.name);
        return false;
    }
    return true;
}

bool parseUrdf (const QString& urdfPath, UrdfModel& model, QStringList& errors)
{
    QFile file (urdfPath);
    if (!file.open (QFile::ReadOnly | QFile::Text)) {
        errors << QString ("Could not open URDF file %1.").arg (urdfPath);
        return false;
    }

    QXmlStreamReader xml (&file);
    if (!xml.readNextStartElement () || xml.name () != "robot") {
        errors << "URDF root element must be <robot>.";
        return false;
    }

    model.robotName = xml.attributes ().value ("name").toString ().trimmed ();
    if (model.robotName.isEmpty ())
        model.robotName = QFileInfo (urdfPath).completeBaseName ();

    while (xml.readNextStartElement ()) {
        const QString tag = xml.name ().toString ();
        if (tag == "link") {
            UrdfLink link;
            link.name = xml.attributes ().value ("name").toString ().trimmed ();
            if (link.name.isEmpty ()) {
                errors << "URDF link without name is not supported.";
                return false;
            }
            model.links[link.name] = link;
            xml.skipCurrentElement ();
        }
        else if (tag == "joint") {
            UrdfJoint joint;
            if (!parseJointElement (xml, joint, errors))
                return false;
            model.joints.push_back (joint);
        }
        else {
            xml.skipCurrentElement ();
        }
    }

    if (xml.hasError ()) {
        errors << QString ("URDF XML parse error: %1").arg (xml.errorString ());
        return false;
    }
    if (model.links.empty ()) {
        errors << "URDF contains no links.";
        return false;
    }
    return true;
}
```

- [ ] **Step 4: Convert parsed joints into a simple `RobotModelSpec`**

Replace the minimal `importFile` body:

```cpp
bool RobotModelUrdfImporter::importFile (const QString& urdfPath,
                                         const UrdfImportOptions& options,
                                         UrdfImportResult& result,
                                         QStringList& errors)
{
    result = UrdfImportResult ();

    UrdfModel model;
    if (!parseUrdf (urdfPath, model, errors))
        return false;

    RobotModelSpec spec;
    spec.robotName = model.robotName.toStdString ();
    spec.saveDirectory = options.saveDirectory.isEmpty () ?
        QFileInfo (urdfPath).absolutePath ().toStdString () :
        options.saveDirectory.toStdString ();
    spec.mode = KinematicsViewMode::JointRPYPos;
    spec.exportDhJointsAdvanced = false;
    spec.showFrameAxes = true;
    spec.generateDrawables = options.generateDrawables;
    spec.generateScene = options.generateScene;
    spec.dynamics.generateDynamicWorkCell = options.generateDynamicWorkCell;
    spec.dynamics.baseFrame = "Base";
    spec.dynamics.baseMaterial = "Steel";

    spec.robotBaseFrame.name = "RobotBase";
    spec.robotBaseFrame.refFrame = "WORLD";
    spec.robotBaseFrame.frameType = SceneFrameType::Fixed;
    spec.robotBaseFrame.rpyDeg = {{0, 0, 0}};
    spec.robotBaseFrame.pos = {{0, 0, 0}};

    for (const UrdfJoint& urdfJoint : model.joints) {
        JointTransformSpec joint;
        joint.name = urdfJoint.name.toStdString ();
        joint.type = jointTypeToPluginType (urdfJoint.type).toStdString ();
        joint.pos = urdfJoint.origin.xyz;
        joint.rpyDeg = urdfRpyToPluginRpyDeg (urdfJoint.origin.rpyRad);
        spec.transformJoints.push_back (joint);

        if (typeToKind (joint.type) == JointKind::Revolute ||
            typeToKind (joint.type) == JointKind::Prismatic) {
            JointLimitSpec limit;
            limit.jointName = joint.name;
            if (typeToKind (joint.type) == JointKind::Revolute) {
                limit.posMin = urdfJoint.limit.hasLower ? radToDeg (urdfJoint.limit.lower) : -180.0;
                limit.posMax = urdfJoint.limit.hasUpper ? radToDeg (urdfJoint.limit.upper) : 180.0;
                limit.velMax = urdfJoint.limit.hasVelocity ? radToDeg (urdfJoint.limit.velocity) : 180.0;
            }
            else {
                limit.posMin = urdfJoint.limit.hasLower ? urdfJoint.limit.lower : -1.0;
                limit.posMax = urdfJoint.limit.hasUpper ? urdfJoint.limit.upper : 1.0;
                limit.velMax = urdfJoint.limit.hasVelocity ? urdfJoint.limit.velocity : 1.0;
            }
            limit.accMax = std::max (limit.velMax, 1.0);
            spec.limits.push_back (limit);

            JointForceLimitSpec force;
            force.jointName = joint.name;
            force.maxForce = urdfJoint.limit.hasEffort ? std::max (urdfJoint.limit.effort, 1e-9) : 100.0;
            spec.dynamics.forceLimits.push_back (force);
        }
    }

    PoseSpec zero;
    zero.name = "Zero";
    zero.q = std::vector< double > (RobotModelXmlWriter::movableJointCount (spec), 0.0);
    spec.poses.push_back (zero);

    RobotModelXmlWriter::refreshDhProjectionFromTransform (spec);
    RobotModelXmlWriter::applyDefaultDrawables (spec);

    result.spec = spec;
    return true;
}
```

- [ ] **Step 5: Run the test and confirm it passes**

Run:

```powershell
cmake --build build --target sdurws_robotmodelbuilder_xmltest --config Debug
```

Run the test executable. Expected: test passes, including the new minimal URDF import block.

- [ ] **Step 6: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "feat: parse URDF joints into robot model spec"
```

---

### Task 3: Respect Serial Chain Order and Warn About Unsupported Branches or Axes

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add a chain-order test**

Add this block after the minimal import test:

```cpp
    {
        const QString dir = QDir::tempPath () + "/robotmodelbuilder_urdf_chain";
        QDir ().mkpath (dir);
        const QString urdfPath = dir + "/chain.urdf";
        QFile file (urdfPath);
        if (!file.open (QFile::WriteOnly | QFile::Text))
            return fail ("Could not create chain URDF test file.");
        QTextStream out (&file);
        out << "<robot name=\"ChainBot\">\n"
            << "  <link name=\"base\" />\n"
            << "  <link name=\"middle\" />\n"
            << "  <link name=\"tool\" />\n"
            << "  <joint name=\"joint_b\" type=\"revolute\"><parent link=\"middle\" />"
            << "<child link=\"tool\" /><origin xyz=\"0 0 0.2\" rpy=\"0 0 0\" /></joint>\n"
            << "  <joint name=\"joint_a\" type=\"revolute\"><parent link=\"base\" />"
            << "<child link=\"middle\" /><origin xyz=\"0 0 0.1\" rpy=\"0 0 0\" /></joint>\n"
            << "</robot>\n";
        file.close ();

        UrdfImportOptions options;
        options.saveDirectory = dir;
        UrdfImportResult result;
        QStringList importErrors;
        if (!RobotModelUrdfImporter::importFile (urdfPath, options, result, importErrors))
            return fail ("Chain URDF import failed: " + importErrors.join ("; "));
        if (result.spec.transformJoints.size () != 2)
            return fail ("Chain URDF should import two transform joints.");
        if (result.spec.transformJoints[0].name != "joint_a" ||
            result.spec.transformJoints[1].name != "joint_b")
            return fail ("URDF joints should be ordered from root to tip.");
    }
```

- [ ] **Step 2: Add graph helpers**

Add helper functions in the anonymous namespace:

```cpp
QString findRootLink (const UrdfModel& model, QStringList& errors)
{
    std::set< QString > childLinks;
    for (const UrdfJoint& joint : model.joints)
        childLinks.insert (joint.childLink);

    QStringList roots;
    for (const auto& item : model.links) {
        if (childLinks.find (item.first) == childLinks.end ())
            roots << item.first;
    }
    if (roots.size () != 1) {
        errors << QString ("URDF must have exactly one root link; found %1.").arg (roots.size ());
        return QString ();
    }
    return roots.front ();
}

std::vector< UrdfJoint > orderedRootChain (const UrdfModel& model,
                                           QStringList& warnings,
                                           QStringList& errors)
{
    const QString root = findRootLink (model, errors);
    if (root.isEmpty ())
        return std::vector< UrdfJoint > ();

    std::map< QString, std::vector< UrdfJoint > > childrenByParent;
    for (const UrdfJoint& joint : model.joints)
        childrenByParent[joint.parentLink].push_back (joint);

    std::vector< UrdfJoint > ordered;
    QString current = root;
    std::set< QString > visitedLinks;
    while (childrenByParent.find (current) != childrenByParent.end ()) {
        std::vector< UrdfJoint > children = childrenByParent[current];
        if (children.size () > 1) {
            std::sort (children.begin (), children.end (),
                       [] (const UrdfJoint& a, const UrdfJoint& b) {
                           return a.name < b.name;
                       });
            warnings << QString ("URDF branch at link %1: importing child joint %2 as the serial chain and skipping %3 sibling branch(es).")
                            .arg (current, children.front ().name)
                            .arg (children.size () - 1);
        }
        const UrdfJoint selected = children.front ();
        if (visitedLinks.find (selected.childLink) != visitedLinks.end ()) {
            errors << QString ("URDF cycle detected at link %1.").arg (selected.childLink);
            return std::vector< UrdfJoint > ();
        }
        visitedLinks.insert (selected.childLink);
        ordered.push_back (selected);
        current = selected.childLink;
    }
    return ordered;
}

bool isDefaultJointAxis (const std::array< double, 3 >& axis)
{
    return std::abs (axis[0]) < 1e-9 && std::abs (axis[1]) < 1e-9 &&
           std::abs (axis[2] - 1.0) < 1e-9;
}
```

- [ ] **Step 3: Use the ordered chain and axis warning**

In `importFile`, replace the `for (const UrdfJoint& urdfJoint : model.joints)` loop source with:

```cpp
    const std::vector< UrdfJoint > orderedJoints =
        orderedRootChain (model, result.warnings, errors);
    if (!errors.isEmpty ())
        return false;

    for (const UrdfJoint& urdfJoint : orderedJoints) {
        if (!isDefaultJointAxis (urdfJoint.axis)) {
            result.warnings << QString ("Joint %1 uses axis %2 %3 %4. Current importer preserves origin but does not re-orient non-Z axes.")
                                    .arg (urdfJoint.name)
                                    .arg (urdfJoint.axis[0])
                                    .arg (urdfJoint.axis[1])
                                    .arg (urdfJoint.axis[2]);
        }
```

Keep the rest of the loop body unchanged.

- [ ] **Step 4: Add a branch warning test**

Add this block:

```cpp
    {
        const QString dir = QDir::tempPath () + "/robotmodelbuilder_urdf_branch";
        QDir ().mkpath (dir);
        const QString urdfPath = dir + "/branch.urdf";
        QFile file (urdfPath);
        if (!file.open (QFile::WriteOnly | QFile::Text))
            return fail ("Could not create branch URDF test file.");
        QTextStream out (&file);
        out << "<robot name=\"BranchBot\">\n"
            << "  <link name=\"base\" />\n"
            << "  <link name=\"arm\" />\n"
            << "  <link name=\"camera\" />\n"
            << "  <joint name=\"arm_joint\" type=\"revolute\"><parent link=\"base\" />"
            << "<child link=\"arm\" /><origin xyz=\"0 0 0.1\" rpy=\"0 0 0\" /></joint>\n"
            << "  <joint name=\"camera_joint\" type=\"fixed\"><parent link=\"base\" />"
            << "<child link=\"camera\" /><origin xyz=\"0.1 0 0\" rpy=\"0 0 0\" /></joint>\n"
            << "</robot>\n";
        file.close ();

        UrdfImportOptions options;
        options.saveDirectory = dir;
        UrdfImportResult result;
        QStringList importErrors;
        if (!RobotModelUrdfImporter::importFile (urdfPath, options, result, importErrors))
            return fail ("Branch URDF import failed: " + importErrors.join ("; "));
        if (result.warnings.isEmpty ())
            return fail ("Branch URDF import should report a branch warning.");
    }
```

- [ ] **Step 5: Run the test and confirm it passes**

Run:

```powershell
cmake --build build --target sdurws_robotmodelbuilder_xmltest --config Debug
```

Run the test executable. Expected: chain order and branch warning tests pass.

- [ ] **Step 6: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "feat: order URDF joints by serial chain"
```

---

### Task 4: Import Visual and Collision Geometry

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add visual/collision test**

Add this block:

```cpp
    {
        const QString dir = QDir::tempPath () + "/robotmodelbuilder_urdf_geometry";
        QDir ().mkpath (dir);
        const QString urdfPath = dir + "/geometry.urdf";
        QFile file (urdfPath);
        if (!file.open (QFile::WriteOnly | QFile::Text))
            return fail ("Could not create geometry URDF test file.");
        QTextStream out (&file);
        out << "<robot name=\"GeoBot\">\n"
            << "  <link name=\"base\" />\n"
            << "  <link name=\"link1\">\n"
            << "    <visual name=\"link1_visual\"><origin xyz=\"0 0 0.05\" rpy=\"0 0 0\" />"
            << "<geometry><box size=\"0.1 0.2 0.3\" /></geometry>"
            << "<material name=\"blue\"><color rgba=\"0.1 0.2 0.3 1\" /></material></visual>\n"
            << "    <collision name=\"link1_collision\"><origin xyz=\"0 0 0.05\" rpy=\"0 0 0\" />"
            << "<geometry><cylinder radius=\"0.04\" length=\"0.2\" /></geometry></collision>\n"
            << "  </link>\n"
            << "  <joint name=\"joint1\" type=\"fixed\"><parent link=\"base\" />"
            << "<child link=\"link1\" /><origin xyz=\"0 0 0.1\" rpy=\"0 0 0\" /></joint>\n"
            << "</robot>\n";
        file.close ();

        UrdfImportOptions options;
        options.saveDirectory = dir;
        UrdfImportResult result;
        QStringList importErrors;
        if (!RobotModelUrdfImporter::importFile (urdfPath, options, result, importErrors))
            return fail ("Geometry URDF import failed: " + importErrors.join ("; "));
        bool foundBox = false;
        for (const DrawableSpec& drawable : result.spec.drawables) {
            if (drawable.name == "link1_visual" && drawable.shape == "Box" &&
                near (drawable.dimensions[0], 0.1) &&
                near (drawable.rgb[0], 0.1)) {
                foundBox = true;
            }
        }
        if (!foundBox)
            return fail ("URDF visual box was not imported as a DrawableSpec.");
        bool foundCollision = false;
        for (const CollisionModelSpec& collision : result.spec.collisionModels) {
            if (collision.name == "link1_collision" && collision.shape == "Cylinder" &&
                near (collision.radius, 0.04) && near (collision.length, 0.2)) {
                foundCollision = true;
            }
        }
        if (!foundCollision)
            return fail ("URDF collision cylinder was not imported as a CollisionModelSpec.");
    }
```

- [ ] **Step 2: Extend parser structures**

Add these structs:

```cpp
struct UrdfGeometry
{
    QString name;
    QString shape;
    QString filePath;
    std::array< double, 3 > dimensions = {{0.1, 0.1, 0.1}};
    double radius = 0.05;
    double length = 0.1;
    UrdfOrigin origin;
    std::array< double, 3 > rgb = {{0.6, 0.6, 0.6}};
};
```

Extend `UrdfLink`:

```cpp
struct UrdfLink
{
    QString name;
    std::vector< UrdfGeometry > visuals;
    std::vector< UrdfGeometry > collisions;
};
```

- [ ] **Step 3: Implement geometry parsing**

Add helpers:

```cpp
bool parseGeometryChild (QXmlStreamReader& xml, UrdfGeometry& geometry, QStringList& errors)
{
    const QString tag = xml.name ().toString ();
    if (tag == "box") {
        geometry.shape = "Box";
        if (!parseVector3 (xml.attributes ().value ("size").toString (), geometry.dimensions)) {
            errors << "Invalid URDF box size.";
            return false;
        }
    }
    else if (tag == "cylinder") {
        geometry.shape = "Cylinder";
        bool okRadius = false;
        bool okLength = false;
        geometry.radius = xml.attributes ().value ("radius").toDouble (&okRadius);
        geometry.length = xml.attributes ().value ("length").toDouble (&okLength);
        if (!okRadius || !okLength) {
            errors << "Invalid URDF cylinder geometry.";
            return false;
        }
    }
    else if (tag == "sphere") {
        geometry.shape = "Sphere";
        bool ok = false;
        geometry.radius = xml.attributes ().value ("radius").toDouble (&ok);
        if (!ok) {
            errors << "Invalid URDF sphere radius.";
            return false;
        }
    }
    else if (tag == "mesh") {
        geometry.shape = "Mesh";
        geometry.filePath = xml.attributes ().value ("filename").toString ();
        if (geometry.filePath.trimmed ().isEmpty ()) {
            errors << "URDF mesh geometry must have filename.";
            return false;
        }
        if (xml.attributes ().hasAttribute ("scale"))
            parseVector3 (xml.attributes ().value ("scale").toString (), geometry.dimensions);
    }
    else {
        return false;
    }
    xml.skipCurrentElement ();
    return true;
}

bool parseVisualOrCollision (QXmlStreamReader& xml, const QString& fallbackName,
                             UrdfGeometry& geometry, QStringList& errors)
{
    geometry.name = xml.attributes ().value ("name").toString ().trimmed ();
    if (geometry.name.isEmpty ())
        geometry.name = fallbackName;
    while (xml.readNextStartElement ()) {
        const QString tag = xml.name ().toString ();
        if (tag == "origin") {
            if (!parseOrigin (xml, geometry.origin, errors))
                return false;
            xml.skipCurrentElement ();
        }
        else if (tag == "geometry") {
            while (xml.readNextStartElement ()) {
                if (!parseGeometryChild (xml, geometry, errors)) {
                    errors << QString ("Unsupported URDF geometry tag %1.").arg (xml.name ().toString ());
                    return false;
                }
            }
        }
        else if (tag == "material") {
            while (xml.readNextStartElement ()) {
                if (xml.name () == "color") {
                    std::array< double, 3 > rgba = {{0.6, 0.6, 0.6}};
                    const QStringList parts = xml.attributes ().value ("rgba").toString ()
                        .split (QRegularExpression ("\\s+"), Qt::SkipEmptyParts);
                    if (parts.size () >= 3) {
                        bool ok0 = false, ok1 = false, ok2 = false;
                        rgba[0] = parts[0].toDouble (&ok0);
                        rgba[1] = parts[1].toDouble (&ok1);
                        rgba[2] = parts[2].toDouble (&ok2);
                        if (ok0 && ok1 && ok2)
                            geometry.rgb = rgba;
                    }
                }
                xml.skipCurrentElement ();
            }
        }
        else {
            xml.skipCurrentElement ();
        }
    }
    if (geometry.shape.isEmpty ()) {
        errors << QString ("URDF geometry %1 has no supported geometry child.").arg (geometry.name);
        return false;
    }
    return true;
}
```

- [ ] **Step 4: Parse link bodies instead of skipping link elements**

Replace the `link` branch in `parseUrdf`:

```cpp
        if (tag == "link") {
            UrdfLink link;
            link.name = xml.attributes ().value ("name").toString ().trimmed ();
            if (link.name.isEmpty ()) {
                errors << "URDF link without name is not supported.";
                return false;
            }
            int visualIndex = 0;
            int collisionIndex = 0;
            while (xml.readNextStartElement ()) {
                if (xml.name () == "visual") {
                    UrdfGeometry geometry;
                    if (!parseVisualOrCollision (xml,
                            link.name + "_visual_" + QString::number (++visualIndex),
                            geometry, errors))
                        return false;
                    link.visuals.push_back (geometry);
                }
                else if (xml.name () == "collision") {
                    UrdfGeometry geometry;
                    if (!parseVisualOrCollision (xml,
                            link.name + "_collision_" + QString::number (++collisionIndex),
                            geometry, errors))
                        return false;
                    link.collisions.push_back (geometry);
                }
                else {
                    xml.skipCurrentElement ();
                }
            }
            model.links[link.name] = link;
        }
```

- [ ] **Step 5: Convert geometry into spec objects**

After the joint conversion loop in `importFile`, add:

```cpp
    for (const auto& item : model.links) {
        const UrdfLink& link = item.second;
        for (const UrdfGeometry& visual : link.visuals) {
            DrawableSpec drawable;
            drawable.name = visual.name.toStdString ();
            drawable.refFrame = linkFrameName (link.name, childLinkToJointName).toStdString ();
            drawable.shape = visual.shape.toStdString ();
            drawable.filePath = visual.filePath.toStdString ();
            drawable.dimensions = visual.dimensions;
            drawable.radius = visual.radius;
            drawable.length = visual.length;
            drawable.rpyDeg = urdfRpyToPluginRpyDeg (visual.origin.rpyRad);
            drawable.pos = visual.origin.xyz;
            drawable.rgb = visual.rgb;
            drawable.collisionModel = false;
            drawable.autoLinkGeometry = false;
            spec.drawables.push_back (drawable);
        }
        for (const UrdfGeometry& collisionGeometry : link.collisions) {
            CollisionModelSpec collision;
            collision.name = collisionGeometry.name.toStdString ();
            collision.refFrame = linkFrameName (link.name, childLinkToJointName).toStdString ();
            collision.shape = collisionGeometry.shape == "Mesh" ? "Mesh" :
                collisionGeometry.shape.toStdString ();
            collision.filePath = collisionGeometry.filePath.toStdString ();
            collision.dimensions = collisionGeometry.dimensions;
            collision.radius = collisionGeometry.radius;
            collision.length = collisionGeometry.length;
            collision.rpyDeg = urdfRpyToPluginRpyDeg (collisionGeometry.origin.rpyRad);
            collision.pos = collisionGeometry.origin.xyz;
            spec.collisionModels.push_back (collision);
        }
    }
```

- [ ] **Step 6: Map URDF link geometry to RobWork frames**

Before converting visual/collision geometry, add this helper in the anonymous namespace:

```cpp
QString linkFrameName (const QString& linkName, const std::map< QString, QString >& childLinkToJointName)
{
    const auto it = childLinkToJointName.find (linkName);
    if (it != childLinkToJointName.end ())
        return it->second;
    return "Base";
}
```

Then create this map in `importFile` after `orderedJoints` has been computed and before geometry conversion:

```cpp
    std::map< QString, QString > childLinkToJointName;
    for (const UrdfJoint& joint : orderedJoints)
        childLinkToJointName[joint.childLink] = joint.name;
```

Use `linkFrameName(link.name, childLinkToJointName)` for `DrawableSpec::refFrame` and `CollisionModelSpec::refFrame`. This keeps original URDF joint names as RobWork device frames and attaches each child link's geometry to the joint that creates that child link. Geometry on the root link is attached to `Base`.

- [ ] **Step 7: Run tests**

Run the build and test commands. Expected: geometry test passes and existing XML validation tests still pass.

- [ ] **Step 8: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "feat: import URDF visual and collision geometry"
```

---

### Task 5: Import Inertial Data

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add inertial test**

Add:

```cpp
    {
        const QString dir = QDir::tempPath () + "/robotmodelbuilder_urdf_inertial";
        QDir ().mkpath (dir);
        const QString urdfPath = dir + "/inertial.urdf";
        QFile file (urdfPath);
        if (!file.open (QFile::WriteOnly | QFile::Text))
            return fail ("Could not create inertial URDF test file.");
        QTextStream out (&file);
        out << "<robot name=\"MassBot\">\n"
            << "  <link name=\"base\" />\n"
            << "  <link name=\"link1\"><inertial><origin xyz=\"0.01 0.02 0.03\" rpy=\"0 0 0\" />"
            << "<mass value=\"2.5\" />"
            << "<inertia ixx=\"0.1\" ixy=\"0.01\" ixz=\"0.02\" iyy=\"0.2\" iyz=\"0.03\" izz=\"0.3\" />"
            << "</inertial></link>\n"
            << "  <joint name=\"joint1\" type=\"revolute\"><parent link=\"base\" />"
            << "<child link=\"link1\" /><origin xyz=\"0 0 0.1\" rpy=\"0 0 0\" />"
            << "<limit lower=\"-1\" upper=\"1\" velocity=\"2\" effort=\"3\" /></joint>\n"
            << "</robot>\n";
        file.close ();

        UrdfImportOptions options;
        options.saveDirectory = dir;
        UrdfImportResult result;
        QStringList importErrors;
        if (!RobotModelUrdfImporter::importFile (urdfPath, options, result, importErrors))
            return fail ("Inertial URDF import failed: " + importErrors.join ("; "));
        bool foundMass = false;
        for (const LinkDynamicsSpec& link : result.spec.dynamics.links) {
            if (link.objectName == "joint1" && near (link.mass, 2.5) &&
                near (link.cog[0], 0.01) && near (link.inertia[0], 0.1) &&
                near (link.inertia[3], 0.01)) {
                foundMass = true;
            }
        }
        if (!foundMass)
            return fail ("URDF inertial data was not imported into LinkDynamicsSpec.");
    }
```

- [ ] **Step 2: Add inertial parser structures**

Add:

```cpp
struct UrdfInertial
{
    bool present = false;
    UrdfOrigin origin;
    double mass = 1.0;
    std::array< double, 6 > inertia = {{0.01, 0.01, 0.01, 0, 0, 0}};
};
```

Extend `UrdfLink`:

```cpp
UrdfInertial inertial;
```

- [ ] **Step 3: Parse inertial**

Add helper:

```cpp
bool parseInertial (QXmlStreamReader& xml, UrdfInertial& inertial, QStringList& errors)
{
    inertial.present = true;
    while (xml.readNextStartElement ()) {
        if (xml.name () == "origin") {
            if (!parseOrigin (xml, inertial.origin, errors))
                return false;
            xml.skipCurrentElement ();
        }
        else if (xml.name () == "mass") {
            bool ok = false;
            inertial.mass = xml.attributes ().value ("value").toDouble (&ok);
            if (!ok || inertial.mass <= 0) {
                errors << "URDF inertial mass must be positive.";
                return false;
            }
            xml.skipCurrentElement ();
        }
        else if (xml.name () == "inertia") {
            const QXmlStreamAttributes attrs = xml.attributes ();
            bool ok[6] = {false, false, false, false, false, false};
            inertial.inertia[0] = attrs.value ("ixx").toDouble (&ok[0]);
            inertial.inertia[3] = attrs.value ("ixy").toDouble (&ok[1]);
            inertial.inertia[4] = attrs.value ("ixz").toDouble (&ok[2]);
            inertial.inertia[1] = attrs.value ("iyy").toDouble (&ok[3]);
            inertial.inertia[5] = attrs.value ("iyz").toDouble (&ok[4]);
            inertial.inertia[2] = attrs.value ("izz").toDouble (&ok[5]);
            for (bool valid : ok) {
                if (!valid) {
                    errors << "URDF inertia must define ixx ixy ixz iyy iyz izz.";
                    return false;
                }
            }
            xml.skipCurrentElement ();
        }
        else {
            xml.skipCurrentElement ();
        }
    }
    return true;
}
```

In link parsing, add:

```cpp
                else if (xml.name () == "inertial") {
                    if (!parseInertial (xml, link.inertial, errors))
                        return false;
                }
```

- [ ] **Step 4: Map child links to joint objects**

Use the `childLinkToJointName` map created in Task 4 before geometry conversion. If the implementing agent did not create it there, add it once near the top of `importFile` after `orderedJoints` is available:

```cpp
    std::map< QString, QString > childLinkToJointName;
    for (const UrdfJoint& joint : orderedJoints)
        childLinkToJointName[joint.childLink] = joint.name;
```

After force limits, add dynamics links:

```cpp
    for (const auto& item : model.links) {
        const UrdfLink& link = item.second;
        if (!link.inertial.present)
            continue;
        const auto jointIt = childLinkToJointName.find (item.first);
        if (jointIt == childLinkToJointName.end ()) {
            result.warnings << QString ("Skipping inertial data for root or non-chain link %1.").arg (item.first);
            continue;
        }
        LinkDynamicsSpec dyn;
        dyn.linkName = item.first.toStdString ();
        dyn.objectName = jointIt->second.toStdString ();
        dyn.mass = link.inertial.mass;
        dyn.cog = link.inertial.origin.xyz;
        dyn.inertia = link.inertial.inertia;
        dyn.estimateInertia = false;
        dyn.material = "Imported";
        spec.dynamics.links.push_back (dyn);
    }
```

If no inertial links were imported, add default dynamics for movable joints:

```cpp
    if (spec.dynamics.links.empty ()) {
        int index = 1;
        for (const JointTransformSpec& joint : spec.transformJoints) {
            if (!isMovable (typeToKind (joint.type)))
                continue;
            LinkDynamicsSpec dyn;
            dyn.linkName = "Link" + std::to_string (index++);
            dyn.objectName = joint.name;
            dyn.mass = 1.0;
            dyn.cog = {{0, 0, 0}};
            dyn.inertia = {{0.01, 0.01, 0.01, 0, 0, 0}};
            dyn.estimateInertia = false;
            dyn.material = "Imported";
            spec.dynamics.links.push_back (dyn);
        }
    }
```

- [ ] **Step 5: Run tests and validate generated DWC XML**

Run build and test. Expected: inertial test passes, and existing DWC tests still pass.

- [ ] **Step 6: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "feat: import URDF inertial data"
```

---

### Task 6: Resolve Mesh Paths Including `package://`

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add path resolution tests**

Add:

```cpp
    {
        const QString dir = QDir::tempPath () + "/robotmodelbuilder_urdf_paths";
        QDir ().mkpath (dir + "/my_robot/meshes");
        QFile mesh (dir + "/my_robot/meshes/link.stl");
        if (!mesh.open (QFile::WriteOnly | QFile::Text))
            return fail ("Could not create dummy mesh file.");
        mesh.write ("solid dummy\nendsolid dummy\n");
        mesh.close ();

        const QString urdfPath = dir + "/pathbot.urdf";
        QFile file (urdfPath);
        if (!file.open (QFile::WriteOnly | QFile::Text))
            return fail ("Could not create package path URDF test file.");
        QTextStream out (&file);
        out << "<robot name=\"PathBot\">\n"
            << "  <link name=\"base\" />\n"
            << "  <link name=\"link1\"><visual><geometry>"
            << "<mesh filename=\"package://my_robot/meshes/link.stl\" />"
            << "</geometry></visual></link>\n"
            << "  <joint name=\"joint1\" type=\"fixed\"><parent link=\"base\" />"
            << "<child link=\"link1\" /></joint>\n"
            << "</robot>\n";
        file.close ();

        UrdfImportOptions options;
        options.saveDirectory = dir;
        options.packageRoots << dir;
        UrdfImportResult result;
        QStringList importErrors;
        if (!RobotModelUrdfImporter::importFile (urdfPath, options, result, importErrors))
            return fail ("Package path URDF import failed: " + importErrors.join ("; "));
        bool resolved = false;
        for (const DrawableSpec& drawable : result.spec.drawables) {
            if (drawable.shape == "Mesh" &&
                QString::fromStdString (drawable.filePath).endsWith ("my_robot/meshes/link.stl")) {
                resolved = true;
            }
        }
        if (!resolved)
            return fail ("package:// mesh path was not resolved.");
    }
```

- [ ] **Step 2: Add path resolver**

Add:

```cpp
QString resolveMeshPath (const QString& rawPath, const QString& urdfPath,
                         const UrdfImportOptions& options, QStringList& warnings)
{
    const QString trimmed = rawPath.trimmed ();
    if (trimmed.startsWith ("package://")) {
        const QString suffix = trimmed.mid (QString ("package://").size ());
        for (const QString& root : options.packageRoots) {
            const QString candidate = QDir (root).absoluteFilePath (suffix);
            if (QFileInfo::exists (candidate))
                return QDir::fromNativeSeparators (candidate);
        }
        warnings << QString ("Could not resolve package mesh path %1; keeping original value.").arg (trimmed);
        return trimmed;
    }
    QFileInfo info (trimmed);
    if (info.isAbsolute ())
        return QDir::fromNativeSeparators (info.absoluteFilePath ());
    const QString relativeToUrdf = QFileInfo (urdfPath).absoluteDir ().absoluteFilePath (trimmed);
    if (QFileInfo::exists (relativeToUrdf))
        return QDir::fromNativeSeparators (relativeToUrdf);
    return QDir::fromNativeSeparators (trimmed);
}
```

- [ ] **Step 3: Apply path resolver during conversion**

In the visual and collision conversion blocks, replace direct assignment of file paths with:

```cpp
drawable.filePath = resolveMeshPath (visual.filePath, urdfPath, options, result.warnings).toStdString ();
```

and:

```cpp
collision.filePath = resolveMeshPath (collisionGeometry.filePath, urdfPath, options, result.warnings).toStdString ();
```

- [ ] **Step 4: Run tests**

Run build and test. Expected: package path test passes.

- [ ] **Step 5: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "feat: resolve URDF mesh paths"
```

---

### Task 7: Validate Imported Spec and Save/Load Generated Scene

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`

- [ ] **Step 1: Add generated XML load test**

Add:

```cpp
    {
        const QString dir = QDir::tempPath () + "/robotmodelbuilder_urdf_load";
        QDir ().mkpath (dir);
        const QString urdfPath = dir + "/loadbot.urdf";
        QFile file (urdfPath);
        if (!file.open (QFile::WriteOnly | QFile::Text))
            return fail ("Could not create load URDF test file.");
        QTextStream out (&file);
        out << "<robot name=\"LoadBot\">\n"
            << "  <link name=\"base\" />\n"
            << "  <link name=\"link1\" />\n"
            << "  <joint name=\"joint1\" type=\"revolute\"><parent link=\"base\" />"
            << "<child link=\"link1\" /><origin xyz=\"0 0 0.2\" rpy=\"0 0 0\" />"
            << "<limit lower=\"-1\" upper=\"1\" velocity=\"1\" effort=\"1\" /></joint>\n"
            << "</robot>\n";
        file.close ();

        UrdfImportOptions options;
        options.saveDirectory = dir;
        UrdfImportResult result;
        QStringList importErrors;
        if (!RobotModelUrdfImporter::importFile (urdfPath, options, result, importErrors))
            return fail ("LoadBot URDF import failed: " + importErrors.join ("; "));
        QStringList saveErrors;
        if (!RobotModelXmlWriter::saveFiles (result.spec, saveErrors))
            return fail ("Imported URDF spec could not be saved: " + saveErrors.join ("; "));
        try {
            const rw::models::WorkCell::Ptr wc =
                rw::loaders::WorkCellLoader::Factory::load (
                    RobotModelXmlWriter::sceneFilePath (result.spec).toStdString ());
            if (wc == NULL)
                return fail ("WorkCellLoader returned null for URDF-generated scene.");
        }
        catch (const std::exception& e) {
            return fail (QString ("WorkCellLoader failed for URDF-generated scene: %1").arg (e.what ()));
        }
    }
```

- [ ] **Step 2: Validate before returning success**

At the end of `importFile`, before `result.spec = spec; return true;`, add:

```cpp
    QStringList validationErrors;
    if (!RobotModelXmlWriter::validate (spec, validationErrors)) {
        errors << "URDF was parsed but produced an invalid RobotModelSpec:";
        errors << validationErrors;
        return false;
    }
```

- [ ] **Step 3: Run tests**

Run build and test. Expected: URDF-generated scene loads with `WorkCellLoader`.

- [ ] **Step 4: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelUrdfImporter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp
git commit -m "test: load scene generated from imported URDF"
```

---

### Task 8: Add Import URDF UI Action

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`

- [ ] **Step 1: Add header include and slot declaration**

In `RobotModelBuilderWidget.hpp`, add slot:

```cpp
void importUrdf ();
```

- [ ] **Step 2: Include importer in widget implementation**

In `RobotModelBuilderWidget.cpp`, add:

```cpp
#include "RobotModelUrdfImporter.hpp"
```

- [ ] **Step 3: Add button in `buildUi()`**

Find the bottom button section where `Generate Preview`, `Save XML`, `Save and Load`, and `Reset` are created. Add:

```cpp
QPushButton* importUrdfBtn = new QPushButton ("Import URDF");
```

Add it before `Generate Preview`:

```cpp
buttonLayout->addWidget (importUrdfBtn);
```

Connect it:

```cpp
connect (importUrdfBtn, SIGNAL (clicked ()), this, SLOT (importUrdf ()));
```

Use the actual local layout variable name in `buildUi()`. In the current file this is near the button creation area around the existing `connect (previewBtn, SIGNAL (clicked ()), this, SLOT (generatePreview ()));`.

- [ ] **Step 4: Implement `importUrdf()`**

Add:

```cpp
void RobotModelBuilderWidget::importUrdf ()
{
    const QString path = QFileDialog::getOpenFileName (
        this, "Import URDF", _saveDirectory->text (), "URDF files (*.urdf *.xml);;All files (*)");
    if (path.isEmpty ())
        return;

    UrdfImportOptions options;
    options.saveDirectory = _saveDirectory->text ().trimmed ();
    if (options.saveDirectory.isEmpty ())
        options.saveDirectory = QFileInfo (path).absolutePath ();
    options.packageRoots << QFileInfo (path).absolutePath ();
    options.generateScene = _generateScene->isChecked ();
    options.generateDrawables = _generateDrawables->isChecked ();
    options.generateDynamicWorkCell = _generateDwc->isChecked ();

    UrdfImportResult result;
    QStringList errors;
    if (!RobotModelUrdfImporter::importFile (path, options, result, errors)) {
        showErrors (errors);
        return;
    }

    fillFromSpec (result.spec);
    generatePreview ();

    if (!result.warnings.isEmpty ()) {
        QMessageBox::information (this, "URDF Import Warnings", result.warnings.join ("\n"));
        setStatus (QString ("URDF imported with %1 warning(s).").arg (result.warnings.size ()));
    }
    else {
        setStatus ("URDF imported.");
    }
}
```

- [ ] **Step 5: Build plugin**

Run:

```powershell
cmake --build build --target sdurws_robotmodelbuilder --config Debug
```

Expected: plugin target builds.

- [ ] **Step 6: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp
git commit -m "feat: add URDF import action to robot model builder"
```

---

### Task 9: Document Behavior and Limitations

**Files:**
- Modify: `docs/RobotModelBuilder.md`

- [ ] **Step 1: Add URDF import section**

Append this section to `docs/RobotModelBuilder.md`:

```markdown
## URDF Import

RobotModelBuilder can import a `.urdf` file as an input format for continued editing in the existing model tables. Import does not create a separate URDF-backed model. The importer converts URDF data into `RobotModelSpec`; after import, `transformJoints` remains the single source of truth, DH stays a read-only projection, and all saving continues through the normal RobWork XML writer.

### Supported URDF Content

- `robot/@name` becomes the robot name.
- `joint` elements with `revolute`, `continuous`, `prismatic`, and `fixed` types become `Revolute`, `Prismatic`, and `FixedFrame` rows.
- `origin xyz/rpy` is converted from URDF meters/radians into the plugin's meters/degrees RPY representation.
- `limit lower/upper/velocity/effort` is imported for movable joints. Revolute position and velocity limits are converted from radians to degrees.
- `visual` box/cylinder/sphere/mesh geometry becomes `DrawableSpec`.
- `collision` box/cylinder/sphere/mesh geometry becomes `CollisionModelSpec`.
- `inertial` mass, center of gravity, and inertia matrix become dynamic link data.

### Limitations

- The first implementation targets serial-chain robots. If a URDF branches, the importer chooses one root-to-tip chain and reports warnings for skipped branches.
- Non-default joint axes are reported as warnings. The imported pose is preserved, but the first implementation does not synthesize extra alignment frames for arbitrary axes.
- `package://` mesh paths are resolved against the URDF directory and package roots supplied by the importer. Unresolved paths are kept and reported as warnings.
```

- [ ] **Step 2: Commit**

```powershell
git add docs/RobotModelBuilder.md
git commit -m "docs: describe URDF import behavior"
```

---

### Task 10: Final Verification

**Files:**
- No new files.

- [ ] **Step 1: Run command-line tests**

Run:

```powershell
cmake --build build --target sdurws_robotmodelbuilder_xmltest --config Debug
```

Run the discovered `sdurws_robotmodelbuilder_xmltest.exe`.

Expected:

```text
All RobotModelBuilder XML tests passed.
```

- [ ] **Step 2: Build the plugin**

Run:

```powershell
cmake --build build --target sdurws_robotmodelbuilder --config Debug
```

Expected: build succeeds with no compile errors.

- [ ] **Step 3: Manual UI smoke test**

Use RobWorkStudio if available:

1. Open RobotModelBuilder.
2. Click `Import URDF`.
3. Choose a simple serial-chain URDF.
4. Confirm the Kinematics, Drawables, Collision Models, Limits, Poses, Dynamics, and XML Preview tabs populate.
5. Click `Save XML`.
6. Click `Save and Load`.
7. Confirm the scene loads or a clear warning explains unsupported URDF content.

- [ ] **Step 4: Final commit if verification changes files**

```powershell
git status --short
```

If only intended files are modified:

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder docs/RobotModelBuilder.md
git commit -m "feat: import URDF robot models"
```

## Self-Review

- Spec coverage: The plan covers importer scaffold, parsing, serial ordering, RPY conversion, limits, visual geometry, collision geometry, inertial data, mesh paths, validation, UI integration, docs, and final verification.
- Placeholder scan: The plan contains no unresolved placeholder tokens and no vague open-ended handling instructions. Limitations are explicit and tested through warnings.
- Type consistency: The plan consistently uses `UrdfImportOptions`, `UrdfImportResult`, `RobotModelUrdfImporter::importFile`, `RobotModelSpec`, `JointTransformSpec`, `DrawableSpec`, `CollisionModelSpec`, and `LinkDynamicsSpec`.
