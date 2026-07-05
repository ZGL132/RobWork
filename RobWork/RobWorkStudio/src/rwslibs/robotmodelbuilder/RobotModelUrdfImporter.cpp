// =============================================================================
//  RobotModelUrdfImporter.cpp
//  说明: URDF(.urdf/.xml) → RobotModelSpec 导入器实现。
//        设计:URDF 是输入格式,transformJoints 仍为唯一真值;
//            DH 仅作为投影视图:导入后 refreshDhProjectionFromTransform(spec)。
//        第一版支持单条串联链 + fixed frame;分支在 Task 3 之后用
//        orderedRootChain 选取一条 root-to-tip 链并对被跳过的兄弟
//        关节给 warnings。
//  URDF 关键约定:
//    * origin xyz 单位米,rpy 单位弧度、语义顺序 (roll around X,
//      pitch around Y, yaw around Z);
//    * axis 单位向量化;
//    * limit lower/upper/velocity 单位 = 关节轴单位(位置/速度);
//    * effort = 力矩上限(Revolute 为 Nm,Prismatic 为 N)。
//  URDF 几何:
//    * visual/collision box/cylinder/sphere/mesh;
//    * mesh filename 可为相对 URDF 的相对路径 或 package:// 路径;
//      路径解析在 Task 6 引入 resolveMeshPath,这里先收 raw 字符串。
// =============================================================================
#include "RobotModelUrdfImporter.hpp"

#include "RobotModelXmlWriter.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include <algorithm>
#include <map>
#include <set>

using namespace rws;

namespace {

// -----------------------------------------------------------------------------
//  私有数据结构(Milestone Plan 2):对应 URDF 一个 robot
// -----------------------------------------------------------------------------
struct UrdfLink
{
    QString name;
};

struct UrdfOrigin
{
    std::array< double, 3 > xyz   = {{0, 0, 0}};
    std::array< double, 3 > rpyRad = {{0, 0, 0}};
};

struct UrdfLimit
{
    bool hasLower   = false;
    bool hasUpper   = false;
    bool hasVelocity = false;
    bool hasEffort  = false;
    double lower    = 0;
    double upper    = 0;
    double velocity = 0;
    double effort   = 0;
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

// -----------------------------------------------------------------------------
//  几何解析(本文件内 forward-decl,实现在 Task 4 步骤 3 引入;
//            这里先 stub 不读 geometry)
// -----------------------------------------------------------------------------
struct UrdfGeometry;        // 占位,后续任务实现

// -----------------------------------------------------------------------------
//  私有解析 helper
// -----------------------------------------------------------------------------

/// 解析 "x y z" -> 3 个 double;失败返回 false
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

/// URDF rpy 是 X-Y-Z 内旋(r,p,y = around X,Y,Z),单位弧度;
/// 插件内部 RPY 是 Z-Y-X(rpyDeg[0]=Z),单位度。
/// 转换语义见 2026-07-05 计划 Task 2 注释。
std::array< double, 3 > urdfRpyToPluginRpyDeg (const std::array< double, 3 >& rpyRad)
{
    return {{radToDeg (rpyRad[2]),    // 插件 rpyDeg[0] (Z) <- URDF yaw
             radToDeg (rpyRad[1]),    // 插件 rpyDeg[1] (Y) <- URDF pitch
             radToDeg (rpyRad[0])}};  // 插件 rpyDeg[2] (X) <- URDF roll
}

/// URDF joint type -> 插件 joint type(目前没用到 ToolFrame:
///   * revolute/continuous -> Revolute
///   * prismatic            -> Prismatic
///   * fixed                -> FixedFrame
///   * 其它(planar/floating等) -> FixedFrame 退路
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
                if (!ok) {
                    errors << QString ("Invalid lower limit on %1.").arg (joint.name);
                    return false;
                }
                joint.limit.hasLower = true;
            }
            if (attrs.hasAttribute ("upper")) {
                joint.limit.upper = attrs.value ("upper").toDouble (&ok);
                if (!ok) {
                    errors << QString ("Invalid upper limit on %1.").arg (joint.name);
                    return false;
                }
                joint.limit.hasUpper = true;
            }
            if (attrs.hasAttribute ("velocity")) {
                joint.limit.velocity = attrs.value ("velocity").toDouble (&ok);
                if (!ok) {
                    errors << QString ("Invalid velocity limit on %1.").arg (joint.name);
                    return false;
                }
                joint.limit.hasVelocity = true;
            }
            if (attrs.hasAttribute ("effort")) {
                joint.limit.effort = attrs.value ("effort").toDouble (&ok);
                if (!ok) {
                    errors << QString ("Invalid effort limit on %1.").arg (joint.name);
                    return false;
                }
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

/// 顶层 <robot>:name 属性 + 依次解析 <link>/<joint>。
/// Task 2:暂不解析 link body(geometry/inertial 在 Task 4/5 引入),只记 link 名;
///        joint 按 xml 出现顺序塞进 model.joints,具体排序在 Task 3 的
///        orderedRootChain 里再做。
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
            // Task 2:link 内部的几何/inertial 暂不读,跳过整段。
            xml.skipCurrentElement ();
            model.links[link.name] = link;
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

}    // namespace

// =============================================================================
//  importFile
//  说明: 任务 2 主入口
//    * 解析 URDF;
//    * 初始化一份 RobotModelSpec 默认字段;
//    * 按 model.joints(暂按出现顺序;Task 3 才会切到 orderedRootChain)
//      顺序填 transformJoints / limits / dynamics.forceLimits;
//    * 末尾加一个名字叫 "Zero" 的零位姿;
//    * refreshDhProjectionFromTransform 后立即 applyDefaultDrawables。
//    Task 4/5 会在此基础上补视觉/碰撞/inertial 转换;
//    Task 7 会在末尾加 validate 兜底。
// =============================================================================
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
    spec.saveDirectory = options.saveDirectory.isEmpty ()
                              ? QFileInfo (urdfPath).absolutePath ().toStdString ()
                              : options.saveDirectory.toStdString ();
    spec.mode                   = KinematicsViewMode::JointRPYPos;
    spec.exportDhJointsAdvanced = false;
    spec.showFrameAxes          = true;
    spec.generateDrawables      = options.generateDrawables;
    spec.generateScene          = options.generateScene;
    spec.dynamics.generateDynamicWorkCell = options.generateDynamicWorkCell;
    spec.dynamics.baseFrame     = "Base";
    spec.dynamics.baseMaterial  = "Steel";

    spec.robotBaseFrame.name      = "RobotBase";
    spec.robotBaseFrame.refFrame  = "WORLD";
    spec.robotBaseFrame.frameType = SceneFrameType::Fixed;
    spec.robotBaseFrame.rpyDeg    = {{0, 0, 0}};
    spec.robotBaseFrame.pos       = {{0, 0, 0}};

    // Task 2:按 model.joints 出现顺序转换(无 chain 排序)
    for (const UrdfJoint& urdfJoint : model.joints) {
        JointTransformSpec joint;
        joint.name   = urdfJoint.name.toStdString ();
        joint.type   = jointTypeToPluginType (urdfJoint.type).toStdString ();
        joint.pos    = urdfJoint.origin.xyz;
        joint.rpyDeg = urdfRpyToPluginRpyDeg (urdfJoint.origin.rpyRad);
        spec.transformJoints.push_back (joint);

        const JointKind kind = typeToKind (joint.type);
        if (kind == JointKind::Revolute || kind == JointKind::Prismatic) {
            JointLimitSpec limit;
            limit.jointName = joint.name;
            if (kind == JointKind::Revolute) {
                limit.posMin =
                    urdfJoint.limit.hasLower ? radToDeg (urdfJoint.limit.lower) : -180.0;
                limit.posMax =
                    urdfJoint.limit.hasUpper ? radToDeg (urdfJoint.limit.upper) : 180.0;
                limit.velMax =
                    urdfJoint.limit.hasVelocity ? radToDeg (urdfJoint.limit.velocity) : 180.0;
            }
            else {
                limit.posMin = urdfJoint.limit.hasLower ? urdfJoint.limit.lower : -1.0;
                limit.posMax = urdfJoint.limit.hasUpper ? urdfJoint.limit.upper : 1.0;
                limit.velMax =
                    urdfJoint.limit.hasVelocity ? urdfJoint.limit.velocity : 1.0;
            }
            limit.accMax = std::max (limit.velMax, 1.0);
            spec.limits.push_back (limit);

            JointForceLimitSpec force;
            force.jointName = joint.name;
            force.maxForce  = urdfJoint.limit.hasEffort
                                  ? std::max (urdfJoint.limit.effort, 1e-9)
                                  : 100.0;
            spec.dynamics.forceLimits.push_back (force);
        }
    }

    PoseSpec zero;
    zero.name = "Zero";
    zero.q    = std::vector< double > (
        static_cast< size_t >(RobotModelXmlWriter::movableJointCount (spec)), 0.0);
    spec.poses.push_back (zero);

    RobotModelXmlWriter::refreshDhProjectionFromTransform (spec);
    RobotModelXmlWriter::applyDefaultDrawables (spec);

    result.spec = spec;
    return true;
}
