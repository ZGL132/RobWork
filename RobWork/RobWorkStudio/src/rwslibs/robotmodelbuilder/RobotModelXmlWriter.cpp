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
const double Pi      = 3.14159265358979323846;

bool isEmpty (const std::string& value)
{
    return QString::fromStdString (value).trimmed ().isEmpty ();
}

QString exportedRobotName (const RobotModelSpec& spec)
{
    return RobotModelXmlWriter::sanitizeFileBaseName (QString::fromStdString (spec.robotName));
}

void appendJointHousings (RobotModelSpec& spec)
{
    for (int i = 0; i < JointCount; ++i) {
        DrawableSpec drawable;
        drawable.name           = "Joint" + std::to_string (i + 1) + "Housing";
        drawable.refFrame       = "Joint" + std::to_string (i + 1);
        drawable.shape          = "Cylinder";
        drawable.radius         = 0.095 - 0.006 * i;
        drawable.length         = 0.10 - 0.006 * i;
        drawable.rpyDeg         = {{0, 0, 0}};
        drawable.pos            = {{0, 0, 0}};
        drawable.rgb            = {{0.45, 0.45, 0.48}};
        drawable.collisionModel = false;
        spec.drawables.push_back (drawable);
    }
}

// 默认圆柱连杆长度/半径，作为 computeLinkPose 计算结果的兜底
void appendLinks (RobotModelSpec& spec)
{
    const double radii[5] = {0.055, 0.05, 0.045, 0.04, 0.035};
    for (int i = 0; i < JointCount - 1; ++i) {
        DrawableSpec drawable;
        drawable.name           = "Link" + std::to_string (i + 1) + "To" + std::to_string (i + 2);
        drawable.refFrame       = "Joint" + std::to_string (i + 1);
        drawable.shape          = "Cylinder";
        drawable.radius         = radii[i];
        drawable.length         = 0;     // 稍后由 computeLinkPose 重新填充
        drawable.rpyDeg         = {{0, 0, 0}};
        drawable.pos            = {{0, 0, 0}};
        drawable.rgb            = {{0.35, 0.45, 0.65}};
        drawable.collisionModel = false;
        drawable.autoLinkGeometry = true;
        spec.drawables.push_back (drawable);
    }
}

void appendDefaultDynamics (RobotModelSpec& spec)
{
    // 默认 6 个 link，关联到 Joint1..Joint6
    const std::string materials[JointCount] = {
        "Steel", "Aluminum", "Aluminum", "Aluminum", "Aluminum", "Aluminum"};
    const double masses[JointCount] = {5.0, 4.0, 3.0, 2.5, 2.0, 1.0};
    for (int i = 0; i < JointCount; ++i) {
        LinkDynamicsSpec link;
        link.linkName      = "Link" + std::to_string (i + 1);
        link.objectName    = "Joint" + std::to_string (i + 1);
        link.mass          = masses[i];
        link.cog           = {{0, 0, 0}};
        link.inertia       = {{0.01, 0.01, 0.01, 0, 0, 0}};
        link.estimateInertia = false;
        link.material      = materials[i];
        spec.dynamics.links.push_back (link);
    }
    for (int i = 0; i < JointCount; ++i) {
        JointForceLimitSpec limit;
        limit.jointName = "Joint" + std::to_string (i + 1);
        limit.maxForce  = 1000.0;
        spec.dynamics.forceLimits.push_back (limit);
    }
    spec.dynamics.baseFrame    = "Base";
    spec.dynamics.baseMaterial = "Steel";
}

// Standard DH / RobWork schilling: p = (a*cos(theta), a*sin(theta), d)
// Here theta is the zero-pose joint angle, i.e. the DH offset for revolute joints.
void dhLinkVector (double a, double d, double offsetDeg, std::array< double, 3 >& v)
{
    const double theta = offsetDeg * Pi / 180.0;
    v[0]               = a * std::cos (theta);
    v[1]               = a * std::sin (theta);
    v[2]               = d;
}

// 根据相邻关节的几何位置算出连杆圆柱的中心、RPY 和长度
void computeLinkPose (const RobotModelSpec& spec, int linkIndex,
                      std::array< double, 3 >& posOut,
                      std::array< double, 3 >& rpyDegOut,
                      double& lengthOut)
{
    posOut     = {{0, 0, 0}};
    rpyDegOut  = {{0, 0, 0}};
    lengthOut  = 0;

    std::array< double, 3 > v = {{0, 0, 0}};
    if (spec.mode == RobotModelMode::JointRPYPos) {
        // link i 是 Joint_{i+1} 到 Joint_{i+2} 的连杆，向量在 Joint_{i+1} 的坐标系下
        const int jointIdx = linkIndex + 1;
        if (jointIdx >= static_cast< int >(spec.transformJoints.size ()))
            return;
        v = spec.transformJoints[jointIdx].pos;
    }
    else {
        // DH 模式：用 RobWork schilling 对应的标准 DH 平移计算
        const int jointIdx = linkIndex + 1;
        if (jointIdx >= static_cast< int >(spec.dhJoints.size ()))
            return;
        const DHJointSpec& j = spec.dhJoints[jointIdx];
        dhLinkVector (j.a, j.d, j.offsetDeg, v);
    }

    const double L = std::sqrt (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    lengthOut = L;
    if (L < 1e-9)
        return;

    posOut[0] = v[0] / 2.0;
    posOut[1] = v[1] / 2.0;
    posOut[2] = v[2] / 2.0;

    // 默认圆柱轴向：局部 +Z
    // 目标方向：d = v / L
    const double dx = v[0] / L;
    const double dy = v[1] / L;
    const double dz = v[2] / L;

    // 轴角表示：绕轴 k 旋转 a，使得 R*(0,0,1) = d
    // k = (0,0,1) × d = (-dy, dx, 0)；a = acos(dz)
    const double kx = -dy;
    const double ky = dx;
    const double kz = 0.0;
    const double kn = std::sqrt (kx * kx + ky * ky + kz * kz);

    double r00 = 1, r01 = 0, r02 = 0;
    double r10 = 0, r11 = 1, r12 = 0;
    double r20 = 0, r21 = 0, r22 = 1;

    if (kn < 1e-9) {
        // d 平行 ±Z 轴
        if (dz < 0) {
            // 绕 X 轴旋转 180°
            r00 = -1;
            r11 = -1;
            // r22 = 1
        }
        // 否则保持单位矩阵
    }
    else {
        const double a = std::acos (std::max (-1.0, std::min (1.0, dz)));
        const double nx = kx / kn;
        const double ny = ky / kn;
        const double nz = kz / kn;
        const double c  = std::cos (a);
        const double s  = std::sin (a);
        const double C  = 1.0 - c;

        // Rodrigues 旋转矩阵
        r00 = c + nx * nx * C;
        r01 = nx * ny * C - nz * s;
        r02 = nx * nz * C + ny * s;
        r10 = ny * nx * C + nz * s;
        r11 = c + ny * ny * C;
        r12 = ny * nz * C - nx * s;
        r20 = nz * nx * C - ny * s;
        r21 = nz * ny * C + nx * s;
        r22 = c + nz * nz * C;
    }

    // ZYX 欧拉角提取：R = RotZ(yaw) * RotY(pitch) * RotX(roll)
    double roll = 0, pitch = 0, yaw = 0;
    pitch = std::asin (std::max (-1.0, std::min (1.0, -r20)));
    if (std::abs (r20) < 0.9999) {
        yaw  = std::atan2 (r10, r00);
        roll = std::atan2 (r21, r22);
    }
    else {
        // pitch ≈ ±90° 时万向锁
        yaw  = 0.0;
        roll = std::atan2 (-r12, r11);
    }
    rpyDegOut[0] = roll * 180.0 / Pi;
    rpyDegOut[1] = pitch * 180.0 / Pi;
    rpyDegOut[2] = yaw * 180.0 / Pi;
}

void applyLinkGeometry (RobotModelSpec& spec)
{
    for (DrawableSpec& drawable : spec.drawables) {
        const QString name = QString::fromStdString (drawable.name);
        if (!name.startsWith ("Link") || !name.contains ("To"))
            continue;
        if (!drawable.autoLinkGeometry)
            continue;
        const QRegularExpressionMatch match =
            QRegularExpression ("^Link(\\d+)To(\\d+)$").match (name);
        if (!match.hasMatch ())
            continue;
        const int linkCounter = match.captured (1).toInt () - 1;
        if (linkCounter < 0 || linkCounter >= JointCount - 1)
            continue;
        std::array< double, 3 > pos, rpy;
        double length = 0;
        computeLinkPose (spec, linkCounter, pos, rpy, length);
        if (length > 1e-9)
            drawable.length = length;
        drawable.pos    = pos;
        drawable.rpyDeg = rpy;
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

    appendJointHousings (spec);
    appendLinks (spec);
    applyLinkGeometry (spec);

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

    PoseSpec ready;
    ready.name = "Ready";
    ready.qDeg = {{0, -90, 90, 0, 0, 0}};
    spec.poses.push_back (ready);

    appendDefaultDynamics (spec);

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
    if (!robotName.isEmpty () && robotName != sanitizeFileBaseName (robotName))
        errors << "Robot name may only contain letters, numbers, underscores, and hyphens.";
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

    std::set< std::string > frameNames = jointNames;
    frameNames.insert ("Base");
    frameNames.insert ("TCP");

    if (spec.generateDrawables) {
        for (const DrawableSpec& drawable : spec.drawables) {
            if (isEmpty (drawable.name))
                errors << "Drawable names must not be empty.";
            if (isEmpty (drawable.refFrame))
                errors << QString ("Drawable %1 requires a reference frame.")
                              .arg (QString::fromStdString (drawable.name));
            else if (frameNames.find (drawable.refFrame) == frameNames.end ())
                errors << QString ("Drawable %1 references unknown frame %2.")
                              .arg (QString::fromStdString (drawable.name),
                                    QString::fromStdString (drawable.refFrame));
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
    }

    for (const JointLimitSpec& limit : spec.limits) {
        if (isEmpty (limit.jointName))
            errors << "Limit rows require a joint name.";
        else if (jointNames.find (limit.jointName) == jointNames.end ())
            errors << QString ("Limit references unknown joint %1.")
                          .arg (QString::fromStdString (limit.jointName));
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

    // 动力学参数校验
    if (spec.dynamics.generateDynamicWorkCell) {
        if (isEmpty (spec.dynamics.baseFrame))
            errors << "Dynamics: baseFrame is required when generating DynamicWorkCell.";
        else if (frameNames.find (spec.dynamics.baseFrame) == frameNames.end ())
            errors << QString ("Dynamics: baseFrame %1 does not exist.")
                          .arg (QString::fromStdString (spec.dynamics.baseFrame));
        if (isEmpty (spec.dynamics.baseMaterial))
            errors << "Dynamics: baseMaterial is required when generating DynamicWorkCell.";

        std::set< std::string > seenObjects;
        for (const LinkDynamicsSpec& link : spec.dynamics.links) {
            if (isEmpty (link.objectName))
                errors << QString ("Dynamics: link '%1' objectName is required.")
                              .arg (QString::fromStdString (link.linkName));
            else if (frameNames.find (link.objectName) == frameNames.end ())
                errors << QString ("Dynamics: link object '%1' does not exist.")
                              .arg (QString::fromStdString (link.objectName));
            if (link.mass <= 0)
                errors << QString ("Dynamics: link '%1' mass must be greater than zero.")
                              .arg (QString::fromStdString (link.objectName));
            for (double c : link.cog) {
                if (!std::isfinite (c))
                    errors << QString ("Dynamics: link '%1' COG must be finite.")
                                  .arg (QString::fromStdString (link.objectName));
            }
            if (!link.estimateInertia) {
                if (!(link.inertia[0] > 0) || !(link.inertia[1] > 0) || !(link.inertia[2] > 0))
                    errors << QString (
                                   "Dynamics: link '%1' Ixx/Iyy/Izz must be positive "
                                   "when EstimateInertia is disabled.")
                              .arg (QString::fromStdString (link.objectName));
            }
            if (!seenObjects.insert (link.objectName).second)
                errors << QString ("Dynamics: duplicate link object '%1'.")
                              .arg (QString::fromStdString (link.objectName));
        }

        for (const JointForceLimitSpec& fl : spec.dynamics.forceLimits) {
            if (isEmpty (fl.jointName))
                errors << "Dynamics: ForceLimit joint name is required.";
            else if (jointNames.find (fl.jointName) == jointNames.end ())
                errors << QString ("Dynamics: ForceLimit references unknown joint %1.")
                              .arg (QString::fromStdString (fl.jointName));
            if (fl.maxForce <= 0)
                errors << QString ("Dynamics: ForceLimit for %1 must be greater than zero.")
                              .arg (QString::fromStdString (fl.jointName));
        }
    }

    return errors.isEmpty ();
}

QString RobotModelXmlWriter::makeSerialDeviceXml (const RobotModelSpec& spec)
{
    QString xml;
    QTextStream out (&xml);
    out << "<SerialDevice name=\"" << exportedRobotName (spec) << "\">\n";
    out << "  <Frame name=\"Base\">\n";
    out << "    <RPY>0 0 0</RPY>\n";
    out << "    <Pos>0 0 0</Pos>\n";
    if (spec.showFrameAxes)
        out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
    out << "  </Frame>\n";

    if (spec.mode == RobotModelMode::DH) {
        for (const DHJointSpec& joint : spec.dhJoints) {
            out << "  <DHJoint name=\"" << QString::fromStdString (joint.name) << "\" alpha=\""
                << number (joint.alphaDeg) << "\" a=\"" << number (joint.a) << "\" d=\""
                << number (joint.d) << "\" offset=\"" << number (joint.offsetDeg)
                << "\" type=\"schilling\"";
            if (spec.showFrameAxes) {
                out << ">\n";
                out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
                out << "  </DHJoint>\n";
            }
            else {
                out << " />\n";
            }
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

    QString tcpRef = "Base";
    if (spec.mode == RobotModelMode::DH && !spec.dhJoints.empty ())
        tcpRef = QString::fromStdString (spec.dhJoints.back ().name);
    else if (!spec.transformJoints.empty ())
        tcpRef = QString::fromStdString (spec.transformJoints.back ().name);
    out << "  <Frame name=\"TCP\" refframe=\"" << tcpRef << "\">\n";
    out << "    <RPY>0 0 0</RPY>\n";
    out << "    <Pos>0 0 0</Pos>\n";
    if (spec.showFrameAxes)
        out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
    out << "  </Frame>\n";

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
    const QString robotName = exportedRobotName (spec);
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

QString RobotModelXmlWriter::makeDynamicWorkCellXml (const RobotModelSpec& spec)
{
    const QString robotName = exportedRobotName (spec);
    QString xml;
    QTextStream out (&xml);
    out << "<DynamicWorkCell workcell=\"" << robotName << "Scene.wc.xml\">\n";
    out << "  <RigidDevice device=\"" << robotName << "\">\n";

    for (const JointForceLimitSpec& fl : spec.dynamics.forceLimits) {
        out << "    <ForceLimit joint=\"" << QString::fromStdString (fl.jointName) << "\">"
            << number (fl.maxForce) << "</ForceLimit>\n";
    }

    out << "    <KinematicBase frame=\"" << QString::fromStdString (spec.dynamics.baseFrame)
        << "\">\n";
    out << "      <MaterialID>" << QString::fromStdString (spec.dynamics.baseMaterial)
        << "</MaterialID>\n";
    out << "    </KinematicBase>\n";

    for (const LinkDynamicsSpec& link : spec.dynamics.links) {
        out << "    <Link object=\"" << QString::fromStdString (link.objectName) << "\">\n";
        out << "      <Mass>" << number (link.mass) << "</Mass>\n";
        out << "      <COG>" << vector3 (link.cog) << "</COG>\n";
        if (link.estimateInertia) {
            out << "      <EstimateInertia />\n";
        }
        else {
            // RobWorkSim 只接受 3 或 9 个数；这里把 6 元惯量数据
            // (Ixx, Iyy, Izz, Ixy, Ixz, Iyz) 展开成行优先 3x3 矩阵：
            //   | Ixx Ixy Ixz |
            //   | Ixy Iyy Iyz |
            //   | Ixz Iyz Izz |
            const QString m = QString::number (link.inertia[0], 'g', 15) + " " +
                              QString::number (link.inertia[3], 'g', 15) + " " +
                              QString::number (link.inertia[4], 'g', 15) + " " +
                              QString::number (link.inertia[3], 'g', 15) + " " +
                              QString::number (link.inertia[1], 'g', 15) + " " +
                              QString::number (link.inertia[5], 'g', 15) + " " +
                              QString::number (link.inertia[4], 'g', 15) + " " +
                              QString::number (link.inertia[5], 'g', 15) + " " +
                              QString::number (link.inertia[2], 'g', 15);
            out << "      <Inertia>" << m << "</Inertia>\n";
        }
        out << "      <MaterialID>" << QString::fromStdString (link.material) << "</MaterialID>\n";
        out << "    </Link>\n";
    }

    out << "  </RigidDevice>\n";
    out << "</DynamicWorkCell>\n";
    return xml;
}

QString RobotModelXmlWriter::serialDeviceFilePath (const RobotModelSpec& spec)
{
    QDir dir (QString::fromStdString (spec.saveDirectory));
    return dir.filePath (exportedRobotName (spec) + ".wc.xml");
}

QString RobotModelXmlWriter::sceneFilePath (const RobotModelSpec& spec)
{
    QDir dir (QString::fromStdString (spec.saveDirectory));
    return dir.filePath (exportedRobotName (spec) + "Scene.wc.xml");
}

QString RobotModelXmlWriter::dynamicWorkCellFilePath (const RobotModelSpec& spec)
{
    QDir dir (QString::fromStdString (spec.saveDirectory));
    return dir.filePath (exportedRobotName (spec) + ".dwc.xml");
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

    if (spec.dynamics.generateDynamicWorkCell) {
        QFile dwcFile (dynamicWorkCellFilePath (spec));
        if (!dwcFile.open (QFile::WriteOnly | QFile::Text)) {
            errors << QString ("Could not write %1").arg (dwcFile.fileName ());
            return false;
        }
        QTextStream dwcStream (&dwcFile);
        dwcStream << makeDynamicWorkCellXml (spec);
        dwcFile.close ();
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
    return value * Pi / 180.0;
}

void RobotModelXmlWriter::computeLinkPose (const RobotModelSpec& spec, int linkIndex,
                                           std::array< double, 3 >& posOut,
                                           std::array< double, 3 >& rpyDegOut,
                                           double& lengthOut)
{
    ::computeLinkPose (spec, linkIndex, posOut, rpyDegOut, lengthOut);
}

void RobotModelXmlWriter::applyLinkGeometry (RobotModelSpec& spec)
{
    ::applyLinkGeometry (spec);
}
