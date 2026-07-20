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
#include <cmath>
#include <map>
#include <set>

using namespace rws;

namespace {

// -----------------------------------------------------------------------------
//  私有数据结构(Milestone Plan 2):对应 URDF 一个 robot
// -----------------------------------------------------------------------------
/// URDF origin(t + rpy),其它结构都会复用
struct UrdfOrigin
{
    std::array< double, 3 > xyz    = {{0, 0, 0}};
    std::array< double, 3 > rpyRad = {{0, 0, 0}};
};

/// URDF <limit>:限位 + 速度 + 力上限
struct UrdfLimit
{
    bool hasLower    = false;
    bool hasUpper    = false;
    bool hasVelocity = false;
    bool hasEffort   = false;
    double lower     = 0;
    double upper     = 0;
    double velocity  = 0;
    double effort    = 0;
};

/// URDF <inertial>:origin + mass + 6 元素 inertia tensor (ixx..izz)。
/// present=true 才被 writer 视为有效数据。
struct UrdfInertial
{
    bool present                       = false;
    UrdfOrigin origin;
    double mass                        = 1.0;
    std::array< double, 6 > inertia    = {{0.01, 0.01, 0.01, 0, 0, 0}};
};

/// forward-decl;完整定义在本文件下方"Task 4"区域。
struct UrdfGeometry;

struct UrdfLink
{
    QString name;
    std::vector< UrdfGeometry > visuals;
    std::vector< UrdfGeometry > collisions;
    UrdfInertial inertial;                          // present=false 表示 link 不带 inertial
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
//  几何(Task 4):visual / collision 共享 UrdfGeometry
// -----------------------------------------------------------------------------
struct UrdfGeometry
{
    QString name;
    QString shape;
    QString filePath;
    std::array< double, 3 > dimensions = {{0.1, 0.1, 0.1}};
    double radius   = 0.05;
    double length   = 0.1;
    UrdfOrigin origin;
    std::array< double, 3 > rgb = {{0.6, 0.6, 0.6}};
};

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

double degToRad (double value)
{
    return value * RobotModelXmlWriter::kPi / 180.0;
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

using Rotation = std::array< double, 9 >;

Rotation identityRotation ()
{
    return {{1, 0, 0, 0, 1, 0, 0, 0, 1}};
}

Rotation multiplyRotation (const Rotation& lhs, const Rotation& rhs)
{
    Rotation out = {{0, 0, 0, 0, 0, 0, 0, 0, 0}};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            for (int k = 0; k < 3; ++k)
                out[row * 3 + col] += lhs[row * 3 + k] * rhs[k * 3 + col];
        }
    }
    return out;
}

Rotation transposeRotation (const Rotation& rotation)
{
    return {{rotation[0], rotation[3], rotation[6],
             rotation[1], rotation[4], rotation[7],
             rotation[2], rotation[5], rotation[8]}};
}

std::array< double, 3 > crossVector (const std::array< double, 3 >& lhs,
                                     const std::array< double, 3 >& rhs)
{
    return {{lhs[1] * rhs[2] - lhs[2] * rhs[1],
             lhs[2] * rhs[0] - lhs[0] * rhs[2],
             lhs[0] * rhs[1] - lhs[1] * rhs[0]}};
}

double vectorNorm (const std::array< double, 3 >& value)
{
    return std::sqrt (value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
}

std::array< double, 3 > normalizedVector (const std::array< double, 3 >& value)
{
    const double norm = vectorNorm (value);
    if (norm < 1e-12)
        return {{0, 0, 1}};
    return {{value[0] / norm, value[1] / norm, value[2] / norm}};
}

Rotation pluginRpyDegToRotation (const std::array< double, 3 >& rpyDeg)
{
    const double a = degToRad (rpyDeg[0]);
    const double b = degToRad (rpyDeg[1]);
    const double c = degToRad (rpyDeg[2]);
    const double ca = std::cos (a);
    const double sa = std::sin (a);
    const double cb = std::cos (b);
    const double sb = std::sin (b);
    const double cc = std::cos (c);
    const double sc = std::sin (c);
    return {{ca * cb, ca * sb * sc - sa * cc, ca * sb * cc + sa * sc,
             sa * cb, sa * sb * sc + ca * cc, sa * sb * cc - ca * sc,
             -sb, cb * sc, cb * cc}};
}

std::array< double, 3 > rotationToPluginRpyDeg (const Rotation& rotation)
{
    const double pitch =
        std::atan2 (-rotation[6],
                    std::sqrt (rotation[0] * rotation[0] + rotation[3] * rotation[3]));
    const double cb = std::cos (pitch);
    double roll = 0.0;
    double yaw  = 0.0;
    if (std::abs (cb) > 1e-9) {
        roll = std::atan2 (rotation[3] / cb, rotation[0] / cb);
        yaw  = std::atan2 (rotation[7] / cb, rotation[8] / cb);
    }
    else {
        yaw = std::atan2 (-rotation[1], rotation[4]);
    }
    auto clean = [] (double value) {
        const double deg = radToDeg (value);
        return std::abs (deg) < 1e-9 ? 0.0 : deg;
    };
    return {{clean (roll), clean (pitch), clean (yaw)}};
}

Rotation axisAlignmentRotation (const std::array< double, 3 >& axis)
{
    const std::array< double, 3 > z = normalizedVector (axis);
    std::array< double, 3 > xHint =
        std::abs (z[0]) < 0.9 ? std::array< double, 3 >{{1, 0, 0}}
                              : std::array< double, 3 >{{0, 1, 0}};
    std::array< double, 3 > y = normalizedVector (crossVector (z, xHint));
    std::array< double, 3 > x = normalizedVector (crossVector (y, z));
    return {{x[0], y[0], z[0],
             x[1], y[1], z[1],
             x[2], y[2], z[2]}};
}

std::array< double, 3 > multiplyRotationVector (const Rotation& rotation,
                                                const std::array< double, 3 >& value)
{
    return {{rotation[0] * value[0] + rotation[1] * value[1] + rotation[2] * value[2],
             rotation[3] * value[0] + rotation[4] * value[1] + rotation[5] * value[2],
             rotation[6] * value[0] + rotation[7] * value[1] + rotation[8] * value[2]}};
}

std::array< double, 6 > rotateInertiaTensor (const std::array< double, 6 >& inertia,
                                             const Rotation& rotation)
{
    const double in[9] = {
        inertia[0], inertia[3], inertia[4],
        inertia[3], inertia[1], inertia[5],
        inertia[4], inertia[5], inertia[2]};

    double tmp[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            for (int k = 0; k < 3; ++k)
                tmp[3 * r + c] += rotation[3 * r + k] * in[3 * k + c];
        }
    }

    double out[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            for (int k = 0; k < 3; ++k)
                out[3 * r + c] += tmp[3 * r + k] * rotation[3 * c + k];
        }
    }

    return {{out[0], out[4], out[8], out[1], out[2], out[5]}};
}

std::array< double, 3 > transformedOriginPos (const UrdfOrigin& origin,
                                              const Rotation& inverseAxisAlignment)
{
    return multiplyRotationVector (inverseAxisAlignment, origin.xyz);
}

std::array< double, 3 > transformedOriginRpyDeg (const UrdfOrigin& origin,
                                                 const Rotation& inverseAxisAlignment)
{
    const Rotation poseRotation =
        pluginRpyDegToRotation (urdfRpyToPluginRpyDeg (origin.rpyRad));
    return rotationToPluginRpyDeg (
        multiplyRotation (inverseAxisAlignment, poseRotation));
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

bool urdfJointIsMovable (const UrdfJoint& joint)
{
    return joint.type.compare ("revolute", Qt::CaseInsensitive) == 0 ||
           joint.type.compare ("continuous", Qt::CaseInsensitive) == 0 ||
           joint.type.compare ("prismatic", Qt::CaseInsensitive) == 0;
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
bool parseUrdf (const QString& urdfPath, UrdfModel& model, QStringList& errors);

// Task 4:visual / collision 子树解析;为 parseUrdf 在 link body 里调用而 forward decl
bool parseVisualOrCollision (QXmlStreamReader& xml, const QString& fallbackName,
                             UrdfGeometry& geometry, QStringList& errors);

// Task 5:urdf inertial 解析
bool parseInertial (QXmlStreamReader& xml, UrdfInertial& inertial, QStringList& errors);

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
            // 解析 link body:<visual>/<collision> -> UrdfGeometry 队列;
            // inertials 在 Task 5 引入。当前其它子元素(inertial等)暂跳过。
            int visualIndex   = 0;
            int collisionIndex = 0;
            while (xml.readNextStartElement ()) {
                if (xml.name () == "visual") {
                    UrdfGeometry geometry;
                    const QString fallback =
                        link.name + "_visual_" + QString::number (++visualIndex);
                    if (!parseVisualOrCollision (xml, fallback, geometry, errors))
                        return false;
                    link.visuals.push_back (geometry);
                }
                else if (xml.name () == "collision") {
                    UrdfGeometry geometry;
                    const QString fallback =
                        link.name + "_collision_" + QString::number (++collisionIndex);
                    if (!parseVisualOrCollision (xml, fallback, geometry, errors))
                        return false;
                    link.collisions.push_back (geometry);
                }
                else if (xml.name () == "inertial") {
                    if (!parseInertial (xml, link.inertial, errors))
                        return false;
                }
                else {
                    xml.skipCurrentElement ();
                }
            }
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

// ===========================================================================
//  Task 3:serial-chain 排序 + branch / 非默认 axis 警告
// ===========================================================================

/// 在 URDF links 中找唯一一个"不是任何 joint 的 child"的 link 当作 root。
/// 找不到 / 多个 root 都报错(因为插件第一版只支持单条串联链)。
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
        errors << QString ("URDF must have exactly one root link; found %1.")
                      .arg (roots.size ());
        return QString ();
    }
    return roots.front ();
}

struct ChainCandidate
{
    std::vector< UrdfJoint > joints;
    int movableCount = 0;
};

bool betterChainCandidate (const ChainCandidate& lhs, const ChainCandidate& rhs)
{
    if (lhs.movableCount != rhs.movableCount)
        return lhs.movableCount > rhs.movableCount;
    if (lhs.joints.size () != rhs.joints.size ())
        return lhs.joints.size () > rhs.joints.size ();

    const QString lhsName = lhs.joints.empty () ? QString () : lhs.joints.front ().name;
    const QString rhsName = rhs.joints.empty () ? QString () : rhs.joints.front ().name;
    return lhsName < rhsName;
}

ChainCandidate bestChainFromLink (
    const QString& link,
    const std::map< QString, std::vector< UrdfJoint > >& childrenByParent,
    const std::set< QString >& visitedLinks,
    QStringList& errors)
{
    const auto childrenIt = childrenByParent.find (link);
    if (childrenIt == childrenByParent.end ())
        return ChainCandidate ();

    ChainCandidate best;
    bool hasBest = false;
    for (const UrdfJoint& child : childrenIt->second) {
        if (visitedLinks.find (child.childLink) != visitedLinks.end ()) {
            errors << QString ("URDF cycle detected at link %1.").arg (child.childLink);
            return ChainCandidate ();
        }

        std::set< QString > nextVisited = visitedLinks;
        nextVisited.insert (child.childLink);
        ChainCandidate candidate =
            bestChainFromLink (child.childLink, childrenByParent, nextVisited, errors);
        if (!errors.isEmpty ())
            return ChainCandidate ();

        candidate.joints.insert (candidate.joints.begin (), child);
        if (urdfJointIsMovable (child))
            ++candidate.movableCount;

        if (!hasBest || betterChainCandidate (candidate, best)) {
            best    = candidate;
            hasBest = true;
        }
    }

    return best;
}

/// 从 root 出发沿 parent -> child 关系走出一条串联链;
/// 每个 link 选第一个 child joint(按 name 字典序),其它兄弟关节通过 warning 上报。
/// 走完 / 出现环 / root 不存在都给出明确报错或空结果。
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

    std::set< QString > visitedLinks;
    visitedLinks.insert (root);
    const ChainCandidate selected =
        bestChainFromLink (root, childrenByParent, visitedLinks, errors);
    if (!errors.isEmpty ())
        return std::vector< UrdfJoint > ();

    QString current = root;
    for (const UrdfJoint& joint : selected.joints) {
        const auto childrenIt = childrenByParent.find (current);
        if (childrenIt != childrenByParent.end () && childrenIt->second.size () > 1) {
            warnings << QString ("URDF branch at link %1: importing child joint %2 as the serial chain and skipping %3 sibling branch(es).")
                            .arg (current, joint.name)
                            .arg (childrenIt->second.size () - 1);
        }
        current = joint.childLink;
    }

    return selected.joints;
}

/// URDF 默认关节轴是 (0,0,1)。非默认可动轴通过旋转 RobWork joint frame
/// 使其 Z 轴对齐 URDF <axis>,然后将 child link 的 visual/collision/inertial
/// 位姿折叠到重定向后的 joint frame 中。
bool isDefaultJointAxis (const std::array< double, 3 >& axis)
{
    return std::abs (axis[0]) < 1e-9 && std::abs (axis[1]) < 1e-9 &&
           std::abs (axis[2] - 1.0) < 1e-9;
}

// ===========================================================================
//  Task 4:visual / collision 几何解析
// ===========================================================================

/// 解析单个 <geometry> 子节点。当前支持 box / cylinder / sphere / mesh;
/// URDF 别的(Plane 等)走"不支持"分支,返回 false,留给上层忽略整段。
bool parseGeometryChild (QXmlStreamReader& xml, UrdfGeometry& geometry, QStringList& errors)
{
    const QString tag = xml.name ().toString ();
    if (tag == "box") {
        geometry.shape = "Box";
        if (!parseVector3 (xml.attributes ().value ("size").toString (),
                           geometry.dimensions)) {
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
        geometry.filePath = xml.attributes ().value ("filename").toString ();
        if (geometry.filePath.trimmed ().isEmpty ()) {
            errors << "URDF mesh geometry must have filename.";
            return false;
        }
        geometry.shape = geometry.filePath.trimmed ().endsWith (".stl", Qt::CaseInsensitive)
                             ? QString ("STL")
                             : QString ("Mesh");
        if (xml.attributes ().hasAttribute ("scale")) {
            std::array< double, 3 > scale;
            if (parseVector3 (xml.attributes ().value ("scale").toString (), scale))
                geometry.dimensions = scale;
        }
    }
    else {
        errors << QString ("Unsupported URDF geometry tag %1.").arg (tag);
        return false;
    }
    xml.skipCurrentElement ();
    return true;
}

/// 解析单个 <visual> 或 <collision>。name 缺省时用 fallbackName。
/// 内部子节点处理:<origin>/<geometry>/<material>。
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
            // <geometry> 内只关心一个本体(box / cylinder / sphere / mesh)
            // + 配套 <material>/<color>(仅 visual 有);遇到不支持的几何返回 false。
            while (xml.readNextStartElement ()) {
                if (!parseGeometryChild (xml, geometry, errors)) {
                    return false;
                }
            }
        }
        else if (tag == "material") {
            while (xml.readNextStartElement ()) {
                if (xml.name () == "color") {
                    std::array< double, 3 > rgba = {{0.6, 0.6, 0.6}};
                    const QStringList parts = xml.attributes ()
                                                  .value ("rgba")
                                                  .toString ()
                                                  .split (QRegularExpression ("\\s+"),
                                                          Qt::SkipEmptyParts);
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
        errors << QString ("URDF geometry %1 has no supported geometry child.")
                      .arg (geometry.name);
        return false;
    }
    return true;
}

/// 把子 link 名映射到"使该 link 出现的 joint 名"。Root 节点 & 在 tree
/// 之外的 link 都映射回 "Base"。
QString linkFrameName (const QString& linkName,
                       const std::map< QString, QString >& childLinkToJointName)
{
    const auto it = childLinkToJointName.find (linkName);
    if (it != childLinkToJointName.end ())
        return it->second;
    return "Base";
}

// ===========================================================================
//  Task 6:mesh 路径解析(package:// + 同目录相对路径)
// ===========================================================================

/// URDF <mesh filename="..."/> 可以是:
///   * package://<pkg>/<sub>:在用户给定的 packageRoots 列表中逐一尝试,
///     首个实际存在文件即返回绝对/规范化路径;没有命中则把原始字符串当
///     兜底返回,并把"无法解析"挂到 warnings。
///   * 绝对路径:原文返回。
///   * 相对路径:尝试在 URDF 同目录下拼接,存在则返回绝对路径,否则保留
///     原始文本。
QString resolveMeshPath (const QString& rawPath, const QString& urdfPath,
                         const UrdfImportOptions& options, QStringList& warnings)
{
    const QString trimmed = rawPath.trimmed ();
    if (trimmed.isEmpty ())
        return trimmed;
    if (trimmed.startsWith ("package://")) {
        const QString suffix = trimmed.mid (QString ("package://").size ());
        const int firstSlash = suffix.indexOf ('/');
        const QString packageRelativeSuffix =
            firstSlash >= 0 ? suffix.mid (firstSlash + 1) : QString ();
        for (const QString& root : options.packageRoots) {
            const QString candidate = QDir (root).absoluteFilePath (suffix);
            if (QFileInfo::exists (candidate))
                return QDir::fromNativeSeparators (candidate);
            if (!packageRelativeSuffix.isEmpty ()) {
                const QString packageRootCandidate =
                    QDir (root).absoluteFilePath (packageRelativeSuffix);
                if (QFileInfo::exists (packageRootCandidate))
                    return QDir::fromNativeSeparators (packageRootCandidate);
            }
        }
        warnings << QString ("Could not resolve package mesh path %1; keeping original value.")
                          .arg (trimmed);
        return trimmed;
    }
    QFileInfo info (trimmed);
    if (info.isAbsolute ())
        return QDir::fromNativeSeparators (info.absoluteFilePath ());
    const QString relativeToUrdf =
        QFileInfo (urdfPath).absoluteDir ().absoluteFilePath (trimmed);
    if (QFileInfo::exists (relativeToUrdf))
        return QDir::fromNativeSeparators (relativeToUrdf);
    return QDir::fromNativeSeparators (trimmed);
}

// ===========================================================================
//  Task 5:urdf <inertial> 解析 -> LinkDynamicsSpec
// ===========================================================================

/// 解析 <inertial>:三个直接子节点 (origin / mass / inertia);
/// inertia 6 个属性(ixx ixy ixz iyy iyz izz)缺一报错。
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

    // Task 3:用 orderedRootChain 取代 model.joints 的出现顺序,
    // 把分支 / 非默认轴 / root 错误 / 环 这些情况记到 result.warnings / errors。
    const std::vector< UrdfJoint > orderedJoints =
        orderedRootChain (model, result.warnings, errors);
    if (!errors.isEmpty ())
        return false;

    std::map< QString, QString > childLinkToFrameName;
    std::map< QString, QString > childLinkToDynamicsJointName;
    std::map< QString, QString > childLinkToJointType;
    std::map< QString, Rotation > childLinkToInverseAxisAlignment;
    std::set< QString > usedTransformNames;

    for (const UrdfJoint& urdfJoint : orderedJoints) {
        const QString pluginJointType = jointTypeToPluginType (urdfJoint.type);
        const bool reorientAxis =
            urdfJointIsMovable (urdfJoint) && !isDefaultJointAxis (urdfJoint.axis);

        const auto parentInvIt =
            childLinkToInverseAxisAlignment.find (urdfJoint.parentLink);
        const Rotation parentInverseAxisAlignment =
            parentInvIt != childLinkToInverseAxisAlignment.end () ? parentInvIt->second :
                                                                    identityRotation ();
        JointTransformSpec joint;
        joint.name   = urdfJoint.name.toStdString ();
        joint.type   = pluginJointType.toStdString ();
        joint.pos    = multiplyRotationVector (parentInverseAxisAlignment,
                                               urdfJoint.origin.xyz);
        Rotation axisAlignment = identityRotation ();
        if (reorientAxis)
            axisAlignment = axisAlignmentRotation (urdfJoint.axis);
        const Rotation originRotation =
            pluginRpyDegToRotation (urdfRpyToPluginRpyDeg (urdfJoint.origin.rpyRad));
        joint.rpyDeg = rotationToPluginRpyDeg (
            multiplyRotation (multiplyRotation (parentInverseAxisAlignment, originRotation),
                              axisAlignment));
        spec.transformJoints.push_back (joint);
        usedTransformNames.insert (urdfJoint.name);

        childLinkToInverseAxisAlignment[urdfJoint.childLink] =
            transposeRotation (axisAlignment);

        childLinkToFrameName[urdfJoint.childLink]         = urdfJoint.name;
        childLinkToDynamicsJointName[urdfJoint.childLink] = urdfJoint.name;
        childLinkToJointType[urdfJoint.childLink]         = pluginJointType;

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

    // Task 4:visual / collision 挂到 child link 对应的实际 frame 上。
    // 非 Z 轴可动关节通过旋转 joint frame 对齐 URDF <axis>,geometry
    // 位姿已折叠到 joint frame 下;root / 链外 link 退化挂到 "Base"。
    for (const auto& item : model.links) {
        const UrdfLink& link = item.second;
        const QString refFrame = linkFrameName (item.first, childLinkToFrameName);

        for (const UrdfGeometry& visual : link.visuals) {
            DrawableSpec drawable;
            drawable.name            = visual.name.toStdString ();
            drawable.refFrame        = refFrame.toStdString ();
            drawable.shape           = visual.shape.toStdString ();
            drawable.filePath        =
                resolveMeshPath (visual.filePath, urdfPath, options, result.warnings)
                    .toStdString ();
            drawable.dimensions      = visual.dimensions;
            drawable.radius          = visual.radius;
            drawable.length          = visual.length;
            {
                const Rotation invAxis =
                    childLinkToInverseAxisAlignment.count (item.first) > 0
                        ? childLinkToInverseAxisAlignment[item.first]
                        : identityRotation ();
                drawable.rpyDeg = transformedOriginRpyDeg (visual.origin, invAxis);
                drawable.pos    = transformedOriginPos (visual.origin, invAxis);
            }
            drawable.rgb             = visual.rgb;
            drawable.collisionModel  = false;
            drawable.autoLinkGeometry = false;
            spec.drawables.push_back (drawable);
        }
        for (const UrdfGeometry& collisionGeometry : link.collisions) {
            CollisionModelSpec collision;
            collision.name        = collisionGeometry.name.toStdString ();
            collision.refFrame    = refFrame.toStdString ();
            // Collision models 也接受 Mesh,本项目 Writer 把 Mesh 视为 Polytope
            // 别名(Milestone 5);其余形状直接复用。
            collision.shape       = (collisionGeometry.shape == "Mesh" ||
                               collisionGeometry.shape == "STL")
                                        ? std::string ("Mesh")
                                        : collisionGeometry.shape.toStdString ();
            collision.filePath    =
                resolveMeshPath (collisionGeometry.filePath, urdfPath, options, result.warnings)
                    .toStdString ();
            collision.dimensions  = collisionGeometry.dimensions;
            collision.radius      = collisionGeometry.radius;
            collision.length      = collisionGeometry.length;
            {
                const Rotation invAxis =
                    childLinkToInverseAxisAlignment.count (item.first) > 0
                        ? childLinkToInverseAxisAlignment[item.first]
                        : identityRotation ();
                collision.rpyDeg = transformedOriginRpyDeg (collisionGeometry.origin,
                                                            invAxis);
                collision.pos    = transformedOriginPos (collisionGeometry.origin,
                                                         invAxis);
            }
            spec.collisionModels.push_back (collision);
        }
    }

    // Task 5:<inertial> -> LinkDynamicsSpec。
    // 重点:URDF 的 <inertial> 挂在子 link 上,但动力学 link 的 objectName
    // 在插件里是可动 joint 名;用 childLinkToJointName 找这个 link 由哪个
    // joint 创建。Root / 不在 chain 上的 link 给 warning 后跳过。
    for (const auto& item : model.links) {
        const UrdfLink& link = item.second;
        if (!link.inertial.present)
            continue;
        const auto jointIt = childLinkToDynamicsJointName.find (item.first);
        if (jointIt == childLinkToDynamicsJointName.end ()) {
            result.warnings << QString ("Skipping inertial data for root or non-chain link %1.")
                                    .arg (item.first);
            continue;
        }
        const auto typeIt = childLinkToJointType.find (item.first);
        if (typeIt == childLinkToJointType.end () ||
            !isMovable (typeToKind (typeIt->second.toStdString ()))) {
            continue;
        }
        LinkDynamicsSpec dyn;
        dyn.linkName        = item.first.toStdString ();
        dyn.objectName      = jointIt->second.toStdString ();
        dyn.mass            = link.inertial.mass;
        {
            const Rotation invAxis =
                childLinkToInverseAxisAlignment.count (item.first) > 0
                    ? childLinkToInverseAxisAlignment[item.first]
                    : identityRotation ();
            dyn.cog     = transformedOriginPos (link.inertial.origin, invAxis);
            dyn.inertia = rotateInertiaTensor (link.inertial.inertia, invAxis);
        }
        dyn.estimateInertia = false;
        dyn.material        = "Imported";
        spec.dynamics.links.push_back (dyn);
    }
    // 没有 URDF inertial 数据时,为每个 movable joint 兜底填一份默认
    // LinkDynamicsSpec,避免 dynamics 表空缺导致后续 SaveAndLoad 失败。
    {
        int index = 1;
        for (const JointTransformSpec& joint : spec.transformJoints) {
            if (!isMovable (typeToKind (joint.type)))
                continue;
            const bool hasDynamics =
                std::find_if (spec.dynamics.links.begin (), spec.dynamics.links.end (),
                              [&joint] (const LinkDynamicsSpec& link) {
                                  return link.objectName == joint.name;
                              }) != spec.dynamics.links.end ();
            if (hasDynamics)
                continue;
            LinkDynamicsSpec dyn;
            dyn.linkName        = "Link" + std::to_string (index++);
            dyn.objectName      = joint.name;
            dyn.mass            = 1.0;
            dyn.cog             = {{0, 0, 0}};
            dyn.inertia         = {{0.01, 0.01, 0.01, 0, 0, 0}};
            dyn.estimateInertia = false;
            dyn.material        = "Imported";
            spec.dynamics.links.push_back (dyn);
        }
    }

    PoseSpec zero;
    zero.name = "Zero";
    zero.q    = std::vector< double > (
        static_cast< size_t >(RobotModelXmlWriter::movableJointCount (spec)), 0.0);
    spec.poses.push_back (zero);

    RobotModelXmlWriter::refreshDhProjectionFromTransform (spec);
    RobotModelXmlWriter::applyDefaultDrawables (spec);
    spec.drawables.erase (
        std::remove_if (spec.drawables.begin (), spec.drawables.end (),
                        [] (const DrawableSpec& drawable) {
                            return drawable.autoLinkGeometry && drawable.length <= 1e-9;
                        }),
        spec.drawables.end ());

    // Task 7:在写盘前调用 XmlWriter.validate 兜底,把任何
    // "URDF 合法但 spec 不合法" 的状态阻挡下来,避免后续 saveFiles
    // / WorkCellLoader 链式失败。
    QStringList validationErrors;
    if (!RobotModelXmlWriter::validate (spec, validationErrors)) {
        errors << "URDF was parsed but produced an invalid RobotModelSpec:";
        errors << validationErrors;
        return false;
    }

    result.spec = spec;
    return true;
}
