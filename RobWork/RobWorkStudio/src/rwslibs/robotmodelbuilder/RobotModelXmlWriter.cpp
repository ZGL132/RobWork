#include "RobotModelXmlWriter.hpp"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

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

    std::vector< std::string > activeJoints;
    if (spec.mode == RobotModelMode::DH) {
        for (const DHJointSpec& joint : spec.dhJoints)
            activeJoints.push_back (joint.name);
    }
    else {
        for (const JointTransformSpec& joint : spec.transformJoints)
            activeJoints.push_back (joint.name);
    }

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
