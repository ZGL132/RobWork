#include "RobotModelXmlWriter.hpp"

#include <QCoreApplication>
#include <QDir>
#include <cmath>
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
    if (!contains (serialXml, "<Frame name=\"Base\">"))
        return fail ("Serial XML missing Base frame.");
    if (!contains (serialXml, "<Frame name=\"TCP\""))
        return fail ("Serial XML missing TCP frame.");
    if (serialXml.count ("<Joint name=\"") != 6)
        return fail ("Serial XML must contain six explicit Joint elements.");
    if (!contains (serialXml,
                   "<Joint name=\"Joint2\" type=\"Revolute\">\n"
                   "    <RPY>0 0 90</RPY>\n"
                   "    <Pos>0.12 0 0</Pos>"))
        return fail ("Joint RPY+Pos defaults should convert DH alpha to RobWork RPY Z-Y-X order.");
    if (contains (serialXml, "<Drawable name=\"Joint1Axis\""))
        return fail ("Default drawables should not include coordinate-axis geometry.");
    if (!contains (serialXml, "<Drawable name=\"Joint1Housing\""))
        return fail ("Serial XML missing default joint housing drawable.");
    if (!contains (serialXml, "<PosLimit refjoint=\"Joint1\""))
        return fail ("Serial XML missing position limit.");
    if (contains (serialXml, "<Q name=\"Home\">"))
        return fail ("Default model should not use Home because RobWork loads it as the initial state.");
    if (!contains (serialXml, "<Q name=\"Ready\">0 -1.5707963267949 1.5707963267949 0 0 0</Q>"))
        return fail ("Ready pose was not converted to radians.");
    if (!contains (sceneXml, "<Include file=\"GenericSixAxis.wc.xml\" />"))
        return fail ("Scene XML missing include.");

    RobotModelSpec dhSpec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    dhSpec.mode           = RobotModelMode::DH;
    const QString dhXml   = RobotModelXmlWriter::makeSerialDeviceXml (dhSpec);
    if (dhXml.count ("<DHJoint name=\"") != 6)
        return fail ("DH XML must contain six DHJoint elements.");
    if (dhXml.count ("<Property name=\"ShowFrameAxis\">true</Property>") != 8)
        return fail ("DH XML should show frame axes for Base, Joint1-6, and TCP.");
    if (!contains (dhXml,
                   "<DHJoint name=\"Joint1\" alpha=\"0\" a=\"0\" d=\"0.35\" offset=\"0\" "
                   "type=\"schilling\">\n"
                   "    <Property name=\"ShowFrameAxis\">true</Property>\n"
                   "  </DHJoint>"))
        return fail ("DH XML should emit ShowFrameAxis inside DHJoint elements.");

    spec.dynamics.generateDynamicWorkCell = true;
    if (!RobotModelXmlWriter::validate (spec, errors))
        return fail ("Default DWC model did not validate: " + errors.join ("; "));

    const QString dwcXml = RobotModelXmlWriter::makeDynamicWorkCellXml (spec);
    if (!contains (dwcXml, "<DynamicWorkCell workcell=\"GenericSixAxisScene.wc.xml\">"))
        return fail ("DWC XML missing DynamicWorkCell root with workcell attribute.");
    if (!contains (dwcXml, "<RigidDevice device=\"GenericSixAxis\">"))
        return fail ("DWC XML missing RigidDevice.");
    if (!contains (dwcXml, "<KinematicBase frame=\"Base\">"))
        return fail ("DWC XML missing KinematicBase.");
    if (!contains (dwcXml, "<Link object=\"Joint1\">"))
        return fail ("DWC XML missing Joint1 link.");
    if (!contains (dwcXml, "<Mass>"))
        return fail ("DWC XML missing Mass.");
    if (!contains (dwcXml, "<COG>"))
        return fail ("DWC XML missing COG.");
    if (!contains (dwcXml, "<Inertia>"))
        return fail ("DWC XML missing Inertia.");
    if (dwcXml.count ("<ForceLimit joint=\"Joint") != 6)
        return fail ("DWC XML must contain six ForceLimit entries.");
    if (!contains (dwcXml, "<MaterialID>Steel</MaterialID>"))
        return fail ("DWC XML missing base MaterialID.");

    RobotModelSpec skewed = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    skewed.transformJoints[1].pos = {{0.3, 0, 0.4}};
    RobotModelXmlWriter::applyLinkGeometry (skewed);
    bool foundLink = false;
    for (const DrawableSpec& d : skewed.drawables) {
        if (QString::fromStdString (d.name) == "Link1To2") {
            foundLink = true;
            const double expectedLength = std::sqrt (0.3 * 0.3 + 0.4 * 0.4);
            if (std::abs (d.length - expectedLength) > 1e-6)
                return fail (QString ("Link1To2 length should be ~%1, got %2")
                                 .arg (expectedLength)
                                 .arg (d.length));
            if (std::abs (d.pos[0] - 0.15) > 1e-6 || std::abs (d.pos[2] - 0.20) > 1e-6)
                return fail (QString ("Link1To2 pos should be 0.15 0 0.20, got %1 %2 %3")
                                 .arg (d.pos[0])
                                 .arg (d.pos[1])
                                 .arg (d.pos[2]));
            const bool isHardcodedX =
                std::abs (d.rpyDeg[0]) < 1e-6 && std::abs (d.rpyDeg[1] - 90.0) < 1e-6 &&
                std::abs (d.rpyDeg[2]) < 1e-6;
            const bool isHardcodedZ =
                std::abs (d.rpyDeg[0]) < 1e-6 && std::abs (d.rpyDeg[1]) < 1e-6 &&
                std::abs (d.rpyDeg[2]) < 1e-6;
            if (isHardcodedX || isHardcodedZ)
                return fail ("Link1To2 RPY still hardcoded for diagonal case.");
            break;
        }
    }
    if (!foundLink)
        return fail ("Could not find Link1To2 drawable.");

    RobotModelSpec standardDh = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    standardDh.mode           = RobotModelMode::DH;
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
                return fail (
                    QString ("Link4To5 pos should be 0 0 0.19 in standard DH, got %1 %2 %3")
                        .arg (d.pos[0])
                        .arg (d.pos[1])
                        .arg (d.pos[2]));
            break;
        }
    }
    if (!foundDhLink)
        return fail ("Could not find Link4To5 drawable in DH mode.");

    RobotModelSpec offsetDh = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    offsetDh.mode           = RobotModelMode::DH;
    offsetDh.dhJoints[1].alphaDeg  = 0;
    offsetDh.dhJoints[1].a         = 0.4;
    offsetDh.dhJoints[1].d         = 0.2;
    offsetDh.dhJoints[1].offsetDeg = 90;
    RobotModelXmlWriter::applyLinkGeometry (offsetDh);
    bool foundOffsetLink = false;
    for (const DrawableSpec& d : offsetDh.drawables) {
        if (QString::fromStdString (d.name) == "Link1To2") {
            foundOffsetLink = true;
            const double expectedLength = std::sqrt (0.4 * 0.4 + 0.2 * 0.2);
            if (std::abs (d.length - expectedLength) > 1e-6)
                return fail ("Link1To2 length should follow standard DH XY/Z projection.");
            if (std::abs (d.pos[0]) > 1e-6 || std::abs (d.pos[1] - 0.2) > 1e-6 ||
                std::abs (d.pos[2] - 0.1) > 1e-6)
                return fail (
                    QString ("Link1To2 pos should be 0 0.2 0.1 for 90 deg offset, got %1 %2 %3")
                        .arg (d.pos[0])
                        .arg (d.pos[1])
                        .arg (d.pos[2]));
            break;
        }
    }
    if (!foundOffsetLink)
        return fail ("Could not find Link1To2 drawable for offset DH case.");

    RobotModelSpec autoDrawable = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    autoDrawable.drawables[6].length = 0.33;
    autoDrawable.drawables[6].rpyDeg = {{10, 20, 30}};
    autoDrawable.drawables[6].pos    = {{0.11, 0.22, 0.33}};
    RobotModelXmlWriter::applyLinkGeometry (autoDrawable);
    if (std::abs (autoDrawable.drawables[6].length - 0.12) > 1e-6 ||
        std::abs (autoDrawable.drawables[6].rpyDeg[1] - 90) > 1e-6 ||
        std::abs (autoDrawable.drawables[6].pos[0] - 0.06) > 1e-6)
        return fail ("Auto Link1To2 drawable geometry should be derived from kinematics.");

    RobotModelSpec badMass = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    badMass.dynamics.generateDynamicWorkCell = true;
    badMass.dynamics.links[0].mass = 0;
    if (RobotModelXmlWriter::validate (badMass, errors))
        return fail ("Zero mass should fail validation.");

    RobotModelSpec badForce = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    badForce.dynamics.generateDynamicWorkCell = true;
    badForce.dynamics.forceLimits[0].maxForce = 0;
    if (RobotModelXmlWriter::validate (badForce, errors))
        return fail ("Zero force limit should fail validation.");

    RobotModelSpec badName = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    badName.robotName = "My Robot";
    if (RobotModelXmlWriter::validate (badName, errors))
        return fail ("Robot names requiring sanitization should fail validation.");

    RobotModelSpec noDrawables = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    noDrawables.generateDrawables = false;
    noDrawables.drawables[0].radius = 0;
    if (!RobotModelXmlWriter::validate (noDrawables, errors))
        return fail ("Disabled drawables should not be validated: " + errors.join ("; "));

    spec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    spec.transformJoints[1].name = spec.transformJoints[0].name;
    if (RobotModelXmlWriter::validate (spec, errors))
        return fail ("Duplicate joint name should fail validation.");

    spec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    spec.drawables[0].radius = 0;
    if (RobotModelXmlWriter::validate (spec, errors))
        return fail ("Zero drawable radius should fail validation.");

    const QString dumpDir = QDir::tempPath () + "/robotmodelbuilder_dump";
    QDir ().mkpath (dumpDir);
    std::cerr << "Dumping to: " << dumpDir.toStdString () << std::endl;
    spec = RobotModelXmlWriter::makeDefaultSixAxisModel (dumpDir);
    spec.dynamics.generateDynamicWorkCell = true;
    if (!RobotModelXmlWriter::saveFiles (spec, errors))
        return fail ("saveFiles failed in dump step: " + errors.join ("; "));
    std::cerr << "Wrote:\n  " << RobotModelXmlWriter::serialDeviceFilePath (spec).toStdString ()
              << "\n  " << RobotModelXmlWriter::sceneFilePath (spec).toStdString () << "\n  "
              << RobotModelXmlWriter::dynamicWorkCellFilePath (spec).toStdString () << std::endl;

    const QString skewedDir = QDir::tempPath () + "/robotmodelbuilder_skewed";
    QDir ().mkpath (skewedDir);
    RobotModelSpec skewedSave = skewed;
    skewedSave.saveDirectory = skewedDir.toStdString ();
    skewedSave.robotName = "SkewedRobot";
    if (!RobotModelXmlWriter::saveFiles (skewedSave, errors))
        return fail ("saveFiles failed for skewed: " + errors.join ("; "));
    std::cerr << "Wrote skewed:\n  "
              << RobotModelXmlWriter::serialDeviceFilePath (skewedSave).toStdString () << std::endl;

    return 0;
}
