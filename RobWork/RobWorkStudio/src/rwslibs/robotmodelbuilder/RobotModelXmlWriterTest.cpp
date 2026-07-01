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

    spec                     = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    spec.drawables[0].radius = 0;
    if (RobotModelXmlWriter::validate (spec, errors))
        return fail ("Zero drawable radius should fail validation.");

    return 0;
}
