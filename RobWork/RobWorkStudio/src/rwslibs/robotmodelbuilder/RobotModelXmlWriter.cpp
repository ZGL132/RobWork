// =============================================================================
//  文件: RobotModelXmlWriter.cpp
//  说明: RobotModelSpec -> XML 的核心实现。文件大体分成 4 块:
//        1) 匿名命名空间:文件内部使用的工具(默认数据填充、向量/姿态计算)
//        2) makeDefaultSixAxisModel : 出厂默认 6 轴机器人数据
//        3) validate                 : 输入校验
//        4) 3 个 make*Xml            : 生成 SerialDevice/Scene/DWC 三段 XML
//        5) saveFiles                : 把上面 3 段写盘
//        6) computeLinkPose / applyLinkGeometry:
//           根据关节几何自动重算 Link{i}To{i+1} 圆柱的中心、姿态、长度
// =============================================================================
#include "RobotModelXmlWriter.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#include <cmath>
#include <set>

using namespace rws;

namespace {
// -----------------------------------------------------------------------------
//  匿名命名空间:仅本文件可见的常量与工具
// -----------------------------------------------------------------------------

/// 6 轴机器人出厂默认关节数(只能用于 makeDefaultSixAxisModel 填默认;
///   实际 spec 接受任意 ≥0 的 transformJoints 数量)
const int DefaultJointCount = 6;

/// 圆周率;与公开常量 RobotModelXmlWriter::kPi 等价,仅本文件使用
static constexpr double Pi = RobotModelXmlWriter::kPi;

/// 字符串是否为空白(把 std::string 转 QString 后判 trim().isEmpty())
bool isEmpty (const std::string& value)
{
    return QString::fromStdString (value).trimmed ().isEmpty ();
}

bool isRevoluteType (const std::string& type)
{
    return QString::fromStdString (type).trimmed ().compare ("Revolute", Qt::CaseInsensitive) == 0;
}

bool isPrismaticType (const std::string& type)
{
    return QString::fromStdString (type).trimmed ().compare ("Prismatic", Qt::CaseInsensitive) == 0;
}

bool isFixedFrameType (const std::string& type)
{
    return QString::fromStdString (type).trimmed ().compare ("FixedFrame", Qt::CaseInsensitive) == 0;
}

bool isToolFrameType (const std::string& type)
{
    return QString::fromStdString (type).trimmed ().compare ("ToolFrame", Qt::CaseInsensitive) == 0;
}

// spec.transformJoints 内全部 frame 名(关节 + Base + TCP),用于校验引用
std::set< std::string > allFrameNames (const RobotModelSpec& spec)
{
    std::set< std::string > names;
    for (const JointTransformSpec& j : spec.transformJoints)
        names.insert (j.name);
    names.insert ("Base");
    names.insert ("TCP");
    return names;
}

// spec.transformJoints 中"可动关节"的下标(其余 FixedFrame/ToolFrame 被跳过)
std::vector< size_t > movableJointIndices (const RobotModelSpec& spec)
{
    std::vector< size_t > result;
    for (size_t i = 0; i < spec.transformJoints.size (); ++i) {
        const std::string t = spec.transformJoints[i].type;
        if (isRevoluteType (t) || isPrismaticType (t))
            result.push_back (i);
    }
    return result;
}

// 在 spec 中查找 jointName 对应的 JointTransformSpec,失败返回 nullptr
const JointTransformSpec* findJoint (const RobotModelSpec& spec, const std::string& jointName)
{
    for (const JointTransformSpec& j : spec.transformJoints) {
        if (j.name == jointName)
            return &j;
    }
    return nullptr;
}

/// 把 spec.robotName 经过文件名安全清洗后返回,作为 XML 节点/文件的"机器人名"
QString exportedRobotName (const RobotModelSpec& spec)
{
    return RobotModelXmlWriter::sanitizeFileBaseName (QString::fromStdString (spec.robotName));
}

/// XML 特殊字符转义:任何来自用户输入或 URDF 的字符串在写入 XML 之前都走这里,
/// 避免 & / < / > / " / ' 五个字符破坏 XML 结构。
QString xmlEscaped (const QString& value)
{
    QString escaped = value;
    escaped.replace ('&', "&amp;");
    escaped.replace ('<', "&lt;");
    escaped.replace ('>', "&gt;");
    escaped.replace ('"', "&quot;");
    escaped.replace ('\'', "&apos;");
    return escaped;
}

QString xmlEscaped (const std::string& value)
{
    return xmlEscaped (QString::fromStdString (value));
}

/// 给 spec 增加默认的 Joint{i+1}Housing 圆柱(关节外壳),用于三维可视化
/// 现在按 transformJoints.size() 生成,而非固定 6 个。
void appendJointHousings (RobotModelSpec& spec)
{
    const int n = static_cast< int >(spec.transformJoints.size ());
    for (int i = 0; i < n; ++i) {
        DrawableSpec drawable;
        drawable.name           = "Joint" + std::to_string (i + 1) + "Housing";
        drawable.refFrame       = spec.transformJoints[i].name;
        drawable.shape          = "Cylinder";
        drawable.filePath       = std::string ();
        drawable.dimensions     = {{0.1, 0.1, 0.1}};
        // 半径/长度随关节号递减,让模型有一个简单的视觉层次
        drawable.radius         = 0.095 - 0.006 * i;
        drawable.length         = 0.10 - 0.006 * i;
        drawable.rpyDeg         = {{0, 0, 0}};
        drawable.pos            = {{0, 0, 0}};
        drawable.rgb            = {{0.45, 0.45, 0.48}};
        drawable.collisionModel = false;
        spec.drawables.push_back (drawable);
    }
}

/// 默认圆柱连杆(Link{i+1}To{i+2})的半径兜底值,长度随后由 computeLinkPose 重算
/// 现在按 transformJoints.size()-1 生成,而非固定 5 个。
void appendLinks (RobotModelSpec& spec)
{
    const int n = static_cast< int >(spec.transformJoints.size ());
    if (n < 2)
        return;
    static const double defaultRadii[6] = {0.055, 0.05, 0.045, 0.04, 0.035, 0.03};
    for (int i = 0; i < n - 1; ++i) {
        DrawableSpec drawable;
        drawable.name             = "Link" + std::to_string (i + 1) + "To" + std::to_string (i + 2);
        drawable.refFrame         = spec.transformJoints[i].name;
        drawable.shape            = "Cylinder";
        drawable.filePath         = std::string ();
        drawable.dimensions       = {{0.1, 0.1, 0.1}};
        const size_t r            = static_cast< size_t >(i) < 6 ? i : 5;
        drawable.radius           = defaultRadii[r];
        drawable.length           = 0;    // 稍后由 computeLinkPose 重新填充
        drawable.rpyDeg           = {{0, 0, 0}};
        drawable.pos              = {{0, 0, 0}};
        drawable.rgb              = {{0.35, 0.45, 0.65}};
        drawable.collisionModel   = false;
        drawable.autoLinkGeometry = true;
        spec.drawables.push_back (drawable);
    }
}

/// 给 spec 增加默认动力学参数:link 与 forceLimit 数 == 可动关节数
/// (Milestone 1:FixedFrame / ToolFrame 不进入动力学 link 列表)
void appendDefaultDynamics (RobotModelSpec& spec)
{
    const std::vector< size_t > movable = movableJointIndices (spec);
    if (movable.empty ()) {
        spec.dynamics.baseFrame    = "Base";
        spec.dynamics.baseMaterial = "Steel";
        return;
    }
    static const std::string defaultMaterials[6] = {
        "Steel", "Aluminum", "Aluminum", "Aluminum", "Aluminum", "Aluminum"};
    static const double defaultMasses[6] = {5.0, 4.0, 3.0, 2.5, 2.0, 1.0};
    for (size_t k = 0; k < movable.size (); ++k) {
        LinkDynamicsSpec link;
        link.linkName        = "Link" + std::to_string (movable[k] + 1);
        link.objectName      = spec.transformJoints[movable[k]].name;
        link.mass            = defaultMasses[std::min< size_t >(k, 5)];
        link.cog             = {{0, 0, 0}};
        link.inertia         = {{0.01, 0.01, 0.01, 0, 0, 0}};
        link.estimateInertia = false;
        link.material        = defaultMaterials[std::min< size_t >(k, 5)];
        spec.dynamics.links.push_back (link);
    }
    for (size_t k = 0; k < movable.size (); ++k) {
        JointForceLimitSpec limit;
        limit.jointName = spec.transformJoints[movable[k]].name;
        limit.maxForce  = 1000.0;
        spec.dynamics.forceLimits.push_back (limit);
    }
    spec.dynamics.baseFrame    = "Base";
    spec.dynamics.baseMaterial = "Steel";
}

// -----------------------------------------------------------------------------
//  dhLinkVector
//  说明: 标准 DH (RobWork schilling 约定)下,父系到本系沿"X + Z"方向的位移:
//          p = (a * cos(theta), a * sin(theta), d)
//        其中 theta 是零位关节角,即 DH 约定里的 offset。
//        这个向量就是 Link{i}To{i+1} 圆柱要跨越的方向与距离。
// -----------------------------------------------------------------------------
void dhLinkVector (double a, double d, double offsetDeg, std::array< double, 3 >& v)
{
    const double theta = offsetDeg * Pi / 180.0;
    v[0]               = a * std::cos (theta);
    v[1]               = a * std::sin (theta);
    v[2]               = d;
}

double normalizedAngleDiffDeg (double lhs, double rhs)
{
    double diff = std::fmod (lhs - rhs, 360.0);
    if (diff > 180.0)
        diff -= 360.0;
    if (diff < -180.0)
        diff += 360.0;
    return std::abs (diff);
}

// -----------------------------------------------------------------------------
//  computeLinkPose
//  说明: 根据相邻关节的几何,算出代表它们的连杆圆柱的中心、RPY、长度:
//        1) 先求出 v = Joint_{i+1} -> Joint_{i+2} 的位移向量;
//        2) 长度 L = |v|,圆柱放在 v 的中点;
//        3) 默认圆柱轴向 +Z,绕轴 k = (0,0,1) x v_hat 旋转 a 角,使 +Z 对齐 v;
//        4) 万一向量与 +Z 平行,则绕 X 轴 180° 翻转;
//        5) 把旋转矩阵拆成 Z-Y-X 欧拉角,作为 Drawable 的 RPY。
//        现在支持 0..(transformJoints.size()-2) 任意 linkIndex。
// -----------------------------------------------------------------------------
void computeLinkPose (const RobotModelSpec& spec, int linkIndex,
                      std::array< double, 3 >& posOut,
                      std::array< double, 3 >& rpyDegOut,
                      double& lengthOut)
{
    posOut     = {{0, 0, 0}};
    rpyDegOut  = {{0, 0, 0}};
    lengthOut  = 0;

    if (spec.transformJoints.size () < 2)
        return;
    const int jointIdx = linkIndex + 1;
    if (jointIdx < 1 || jointIdx >= static_cast< int >(spec.transformJoints.size ()))
        return;
    const std::array< double, 3 > v = spec.transformJoints[jointIdx].pos;

    const double L = std::sqrt (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    lengthOut = L;
    if (L < 1e-9)
        return;    // 退化为零向量时,保持零长度零姿态即可

    posOut[0] = v[0] / 2.0;
    posOut[1] = v[1] / 2.0;
    posOut[2] = v[2] / 2.0;

    // 默认圆柱轴向:局部 +Z
    // 目标方向:d = v / L
    const double dx = v[0] / L;
    const double dy = v[1] / L;
    const double dz = v[2] / L;

    // 轴角表示:绕轴 k 旋转 a,使得 R*(0,0,1) = d
    // k = (0,0,1) x d = (-dy, dx, 0);a = acos(dz)
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

    // RobWork RPY(a,b,c): a around Z, b around Y, c around X.
    double zAngle = 0, yAngle = 0, xAngle = 0;
    yAngle = std::asin (std::max (-1.0, std::min (1.0, -r20)));
    if (std::abs (r20) < 0.9999) {
        zAngle = std::atan2 (r10, r00);
        xAngle = std::atan2 (r21, r22);
    }
    else {
        // yAngle ~= +/-90 deg: gimbal lock
        zAngle = 0.0;
        xAngle = std::atan2 (-r12, r11);
    }
    rpyDegOut[0] = zAngle * 180.0 / Pi;
    rpyDegOut[1] = yAngle * 180.0 / Pi;
    rpyDegOut[2] = xAngle * 180.0 / Pi;
}

/// 把 spec 里所有 autoLinkGeometry=true 的 Link{i}To{i+1} Drawable 用上面的算法重算一次
/// 现在允许 transformJoints.size() >= 2 的任意长度。
void applyLinkGeometry (RobotModelSpec& spec)
{
    const int maxLinks = static_cast< int >(spec.transformJoints.size ()) - 1;
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
        if (linkCounter < 0 || linkCounter >= maxLinks)
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

// =============================================================================
//  makeDefaultSixAxisModel
//  说明: 出厂默认的"通用 6 轴机器人"数据:
//        - 6 个 JointRPYPos + DH 关节(给出常用尺寸)
//        - 6 个关节外壳 + 5 个连杆圆柱
//        - 默认限位 / Zero + Ready 两个预设位姿
//        - 默认动力学参数(6 个 link + 6 个力限)
// =============================================================================
RobotModelSpec RobotModelXmlWriter::makeDefaultSixAxisModel (const QString& saveDirectory)
{
    RobotModelSpec spec;
    spec.robotName         = "GenericSixAxis";
    spec.saveDirectory     = saveDirectory.toStdString ();
    spec.mode              = KinematicsViewMode::JointRPYPos;
    spec.showFrameAxes     = true;
    spec.generateDrawables = true;
    spec.generateScene     = true;

    // 一组能跑出像样姿态的默认关节尺寸
    const double alphaDeg[DefaultJointCount]  = {0, 90, 0, 90, -90, 90};
    const double offsetDeg[DefaultJointCount] = {0, 0, 0, 0, 0, 0};
    const double pos[DefaultJointCount][3]    = {
        {0, 0, 0.35}, {0.12, 0, 0}, {0.52, 0, 0},
        {0.42, 0, 0}, {0, 0, 0.38}, {0, 0, 0.12}};
    for (int i = 0; i < DefaultJointCount; ++i) {
        DHJointSpec dh;
        dh.name      = "Joint" + std::to_string (i + 1);
        dh.alphaDeg  = alphaDeg[i];
        dh.a         = pos[i][0];
        dh.d         = pos[i][2];
        dh.offsetDeg = offsetDeg[i];
        spec.dhJoints.push_back (dh);

        JointTransformSpec joint;
        joint.name   = dh.name;
        joint.type   = "Revolute";
        // 把 DH 的 alpha/offset 翻译成 RobWork 的 RPY(Z-Y-X 顺序)
        joint.rpyDeg = {{dh.offsetDeg, 0, dh.alphaDeg}};
        joint.pos    = {{pos[i][0], pos[i][1], pos[i][2]}};
        spec.transformJoints.push_back (joint);
    }

    appendJointHousings (spec);
    appendLinks (spec);
    applyLinkGeometry (spec);    // 让连杆圆柱的位姿与上面关节参数保持一致

    // 默认关节限位(单位中立:Revolute 用度,Prismatic 用米)
    const double posMin[DefaultJointCount] = {-180, -120, -150, -180, -120, -360};
    const double posMax[DefaultJointCount] = {180, 120, 150, 180, 120, 360};
    const double vel[DefaultJointCount]    = {120, 120, 120, 180, 180, 240};
    const double acc[DefaultJointCount]    = {360, 360, 360, 540, 540, 720};
    for (int i = 0; i < DefaultJointCount; ++i) {
        JointLimitSpec limit;
        limit.jointName = "Joint" + std::to_string (i + 1);
        limit.posMin    = posMin[i];
        limit.posMax    = posMax[i];
        limit.velMax    = vel[i];
        limit.accMax    = acc[i];
        spec.limits.push_back (limit);
    }

    // 默认位姿(每行 q 仅含可动关节,这里 6 个全为 Revolute,所以 q 长度为 6)
    PoseSpec zero;
    zero.name = "Zero";
    zero.q    = std::vector< double > (DefaultJointCount, 0.0);
    spec.poses.push_back (zero);

    PoseSpec ready;
    ready.name = "Ready";
    ready.q    = {0.0, -90.0, 90.0, 0.0, 0.0, 0.0};
    spec.poses.push_back (ready);

    appendDefaultDynamics (spec);

    // ---- Milestone 3:默认 RobotBase + 4 个常用场景 frame ----
    spec.robotBaseFrame.name      = "RobotBase";
    spec.robotBaseFrame.refFrame  = "WORLD";
    spec.robotBaseFrame.frameType = SceneFrameType::Fixed;
    spec.robotBaseFrame.daf       = false;
    spec.robotBaseFrame.poseMode  = PoseMode::RPYPos;
    spec.robotBaseFrame.rpyDeg    = {{0, 0, 0}};
    spec.robotBaseFrame.pos       = {{0, 0, 0}};

    {
        FrameSpec f;
        f.name      = "Table";
        f.refFrame  = "WORLD";
        f.frameType = SceneFrameType::Fixed;
        f.pos       = {{0.7, 0, 0.35}};
        spec.sceneFrames.push_back (f);
    }
    {
        FrameSpec f;
        f.name      = "Workpiece";
        f.refFrame  = "Table";
        f.frameType = SceneFrameType::Fixed;
        f.daf       = true;
        f.pos       = {{0, 0, 0.08}};
        spec.sceneFrames.push_back (f);
    }
    {
        FrameSpec f;
        f.name      = "CameraFrame";
        f.refFrame  = "RobotBase";
        f.frameType = SceneFrameType::Normal;
        f.rpyDeg    = {{180, 0, 0}};
        f.pos       = {{0.2, 0.1, 1.2}};
        spec.sceneFrames.push_back (f);
    }
    {
        FrameSpec f;
        f.name      = "MovableBox";
        f.refFrame  = "WORLD";
        f.frameType = SceneFrameType::Movable;
        f.daf       = true;
        f.pos       = {{0.1, 0.2, 0.3}};
        spec.sceneFrames.push_back (f);
    }

    // ---- Milestone 3.5:给默认 3 个场景 frame 各挂一个 Box 几何体 ----
    {
        SceneGeometrySpec g;
        g.name          = "TableTop";
        g.refFrame      = "Table";
        g.kind          = GeometryKind::Box;
        g.size          = {{1.2, 0.8, 0.05}};
        g.pos           = {{0, 0, 0}};
        g.rgb           = {{0.55, 0.55, 0.55}};
        g.collisionModel = true;
        spec.sceneGeometries.push_back (g);
    }
    {
        SceneGeometrySpec g;
        g.name          = "WorkpieceBox";
        g.refFrame      = "Workpiece";
        g.kind          = GeometryKind::Box;
        g.size          = {{0.12, 0.08, 0.05}};
        g.rgb           = {{0.2, 0.55, 0.8}};
        g.collisionModel = true;
        spec.sceneGeometries.push_back (g);
    }
    {
        SceneGeometrySpec g;
        g.name          = "MovableBoxGeom";
        g.refFrame      = "MovableBox";
        g.kind          = GeometryKind::Box;
        g.size          = {{0.08, 0.08, 0.08}};
        g.rgb           = {{0.8, 0.35, 0.2}};
        g.collisionModel = true;
        spec.sceneGeometries.push_back (g);
    }

    // ---- Milestone 6:CollisionSetup / ProximitySetup 默认值 ----
    // 把 includes 留空,Writer 会在 makeSceneXml 自动注入默认 Include 项,
    // 避免 robotName 改了文件名还指向旧 include 的陷阱。
    spec.collisionSetup.enabled                  = true;
    spec.collisionSetup.file                     = "CollisionSetup.xml";
    spec.collisionSetup.excludeAdjacentLinkPairs = true;
    spec.collisionSetup.excludeStaticPairs       = false;

    spec.proximitySetup.enabled                  = false;
    spec.proximitySetup.file                     = "ProximitySetup.xml";
    spec.proximitySetup.useIncludeAll            = true;
    spec.proximitySetup.useExcludeStaticPairs    = false;

    return spec;
}

// =============================================================================
//  sanitizeFileBaseName
//  说明: 把任意字符串清洗为合法文件名(字母/数字/_/-),其他字符替换为 _。
// =============================================================================
QString RobotModelXmlWriter::sanitizeFileBaseName (const QString& name)
{
    QString result = name.trimmed ();
    result.replace (QRegularExpression ("[^A-Za-z0-9_\\-]"), "_");
    return result;
}

// =============================================================================
//  validate
//  说明: 校验 spec 的合法性,把每条错误追加到 errors。
//        校验范围:机器人名/保存目录存在/关节数=6/无空名/无重复名/
//        Drawable 几何合法/RGB ∈ [0,1]/关节限位合法/DWC 数据合法。
// =============================================================================
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

    // ---- Milestone 1+2:关节列表(Milestone 1:类型必须是可识别的 4 种之一;
    //                                  Milestone 2:可变数量,允许 0 / 3 / 6 / 7 等)----
    const std::vector< size_t > movable = movableJointIndices (spec);
    std::set< std::string > movableNames;
    for (size_t idx : movable)
        movableNames.insert (spec.transformJoints[idx].name);

    std::set< std::string > allNames;
    for (const JointTransformSpec& j : spec.transformJoints) {
        if (isEmpty (j.name))
            errors << "Joint names must not be empty.";
        if (!allNames.insert (j.name).second)
            errors << QString ("Duplicate frame name: %1").arg (QString::fromStdString (j.name));

        const QString t = QString::fromStdString (j.type).trimmed ();
        if (t.isEmpty ())
            errors << QString ("Joint %1 requires a type.").arg (QString::fromStdString (j.name));
        else if (!isRevoluteType (j.type) && !isPrismaticType (j.type) &&
                 !isFixedFrameType (j.type) && !isToolFrameType (j.type))
            errors << QString ("Joint %1 has unsupported type '%2' "
                               "(expected Revolute/Prismatic/FixedFrame/ToolFrame).")
                          .arg (QString::fromStdString (j.name))
                          .arg (t);
    }

    if (spec.exportDhJointsAdvanced)
        canExportDhJoints (spec, &errors);

    // 全部 Frame 名(关节 + Base + TCP),用于校验 Drawable/Dynamics 引用
    const std::set< std::string > frameNames = allFrameNames (spec);

    // ---- Milestone 3:RobotBase 与场景 frame 校验 ----
    if (isEmpty (spec.robotBaseFrame.name))
        errors << "RobotBase frame name must not be empty.";
    if (spec.generateScene) {
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
            if (!frame.name.empty () && (frame.name == "Base" || frame.name == "TCP" ||
                                         allNames.find (frame.name) != allNames.end ()))
                errors << QString ("Scene frame %1 collides with a device frame name.")
                              .arg (QString::fromStdString (frame.name));
        }

        std::set< std::string > declaredSceneRefs;
        declaredSceneRefs.insert ("WORLD");
        declaredSceneRefs.insert ("RobotBase");
        for (const FrameSpec& frame : spec.sceneFrames) {
            if (isEmpty (frame.refFrame))
                errors << QString ("Scene frame %1 requires a refframe.")
                              .arg (QString::fromStdString (frame.name));
            else if (declaredSceneRefs.find (frame.refFrame) == declaredSceneRefs.end ())
                errors << QString ("Scene frame %1 references frame %2 before it is declared.")
                              .arg (QString::fromStdString (frame.name),
                                    QString::fromStdString (frame.refFrame));
            if (frame.name == frame.refFrame)
                errors << QString ("Scene frame %1 must not reference itself.")
                              .arg (QString::fromStdString (frame.name));
            if (!isEmpty (frame.name))
                declaredSceneRefs.insert (frame.name);
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

        // ---- Milestone 3.5:场景几何体校验(refframe + 尺寸 + RGB)----
        std::set< std::string > sceneFrameRefs;
        sceneFrameRefs.insert ("WORLD");
        sceneFrameRefs.insert ("RobotBase");
        for (const FrameSpec& frame : spec.sceneFrames)
            sceneFrameRefs.insert (frame.name);

        std::set< std::string > sceneGeometryNames;
        for (const SceneGeometrySpec& geometry : spec.sceneGeometries) {
            if (isEmpty (geometry.name))
                errors << "Scene geometry names must not be empty.";
            else if (!sceneGeometryNames.insert (geometry.name).second)
                errors << QString ("Duplicate scene geometry name: %1.")
                              .arg (QString::fromStdString (geometry.name));
            if (isEmpty (geometry.refFrame))
                errors << QString ("Scene geometry %1 requires a refframe.")
                              .arg (QString::fromStdString (geometry.name));
            else if (sceneFrameRefs.find (geometry.refFrame) == sceneFrameRefs.end ())
                errors << QString ("Scene geometry %1 references unknown frame %2.")
                              .arg (QString::fromStdString (geometry.name),
                                    QString::fromStdString (geometry.refFrame));
            for (double color : geometry.rgb) {
                if (color < 0 || color > 1)
                    errors << QString ("Scene geometry %1 RGB values must be between 0 and 1.")
                                  .arg (QString::fromStdString (geometry.name));
            }
            if (geometry.kind == GeometryKind::Box &&
                (!(geometry.size[0] > 0) || !(geometry.size[1] > 0) || !(geometry.size[2] > 0)))
                errors << QString ("Scene geometry %1 Box size must be greater than zero.")
                              .arg (QString::fromStdString (geometry.name));
            if ((geometry.kind == GeometryKind::Cylinder || geometry.kind == GeometryKind::Sphere ||
                 geometry.kind == GeometryKind::Cone) &&
                !(geometry.radius > 0))
                errors << QString ("Scene geometry %1 radius must be greater than zero.")
                              .arg (QString::fromStdString (geometry.name));
            if ((geometry.kind == GeometryKind::Cylinder || geometry.kind == GeometryKind::Cone) &&
                !(geometry.length > 0))
                errors << QString ("Scene geometry %1 length must be greater than zero.")
                              .arg (QString::fromStdString (geometry.name));
            if (geometry.kind == GeometryKind::Plane &&
                (!(geometry.size[0] > 0) || !(geometry.size[1] > 0)))
                errors << QString ("Scene geometry %1 Plane size must be greater than zero.")
                              .arg (QString::fromStdString (geometry.name));
            if ((geometry.kind == GeometryKind::STL || geometry.kind == GeometryKind::Mesh ||
                 geometry.kind == GeometryKind::Polytope) && isEmpty (geometry.file))
                errors << QString ("Scene geometry %1 %2 requires a file path.")
                              .arg (QString::fromStdString (geometry.name),
                                    geometryKindToString (geometry.kind));
        }
    }

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
            // Milestone 4:按 shape 维度校验,Unknown → 拦截,
            // 避免把残缺几何也照样写出 XML
            const GeometryKind kind = geometryKindFromString (drawable.shape);
            if (kind == GeometryKind::Unknown) {
                errors << QString ("Drawable %1 has unsupported shape %2.")
                              .arg (QString::fromStdString (drawable.name),
                                    QString::fromStdString (drawable.shape));
            }
            if (kind == GeometryKind::Box &&
                (!(drawable.dimensions[0] > 0) || !(drawable.dimensions[1] > 0) ||
                 !(drawable.dimensions[2] > 0))) {
                errors << QString ("Drawable %1 Box dimensions must be greater than zero.")
                              .arg (QString::fromStdString (drawable.name));
            }
            if (kind == GeometryKind::Plane &&
                (!(drawable.dimensions[0] > 0) || !(drawable.dimensions[1] > 0))) {
                errors << QString ("Drawable %1 Plane dimensions must be greater than zero.")
                              .arg (QString::fromStdString (drawable.name));
            }
            if ((kind == GeometryKind::Cylinder || kind == GeometryKind::Sphere ||
                 kind == GeometryKind::Cone) && !(drawable.radius > 0)) {
                errors << QString ("Drawable %1 radius must be greater than zero.")
                              .arg (QString::fromStdString (drawable.name));
            }
            if ((kind == GeometryKind::Cylinder || kind == GeometryKind::Cone) &&
                !(drawable.length > 0)) {
                errors << QString ("Drawable %1 length must be greater than zero.")
                              .arg (QString::fromStdString (drawable.name));
            }
            if ((kind == GeometryKind::STL || kind == GeometryKind::Mesh ||
                 kind == GeometryKind::Polytope) && isEmpty (drawable.filePath)) {
                errors << QString ("Drawable %1 file path is required for %2 geometry.")
                              .arg (QString::fromStdString (drawable.name),
                                    QString::fromStdString (drawable.shape));
            }
            // 维度 / 半径 / 长度必须是有限数,避免 NaN/Inf 污染 XML
            for (double value : drawable.dimensions) {
                if (!std::isfinite (value))
                    errors << QString ("Drawable %1 dimensions must be finite.")
                                  .arg (QString::fromStdString (drawable.name));
            }
            if (!std::isfinite (drawable.radius) || !std::isfinite (drawable.length))
                errors << QString ("Drawable %1 radius/length must be finite.")
                              .arg (QString::fromStdString (drawable.name));
            for (double color : drawable.rgb) {
                if (color < 0 || color > 1)
                    errors << QString ("Drawable %1 RGB values must be between 0 and 1.")
                                  .arg (QString::fromStdString (drawable.name));
            }
        }
    }

    // ---- Milestone 5:独立 CollisionModel 校验(不挂 generateDrawables)----
    std::set< std::string > collisionNames;
    for (const CollisionModelSpec& collision : spec.collisionModels) {
        if (isEmpty (collision.name))
            errors << "CollisionModel names must not be empty.";
        else if (!collisionNames.insert (collision.name).second)
            errors << QString ("Duplicate CollisionModel name: %1.")
                          .arg (QString::fromStdString (collision.name));

        if (isEmpty (collision.refFrame))
            errors << QString ("CollisionModel %1 requires a reference frame.")
                          .arg (QString::fromStdString (collision.name));
        else if (frameNames.find (collision.refFrame) == frameNames.end ())
            errors << QString ("CollisionModel %1 references unknown frame %2.")
                          .arg (QString::fromStdString (collision.name),
                                QString::fromStdString (collision.refFrame));

        const GeometryKind kind = geometryKindFromString (collision.shape);
        if (!isCollisionModelShapeSupported (kind))
            errors << QString ("CollisionModel %1 has unsupported shape %2.")
                          .arg (QString::fromStdString (collision.name),
                                QString::fromStdString (collision.shape));

        if (kind == GeometryKind::Box &&
            (!(collision.dimensions[0] > 0) || !(collision.dimensions[1] > 0) ||
             !(collision.dimensions[2] > 0))) {
            errors << QString ("CollisionModel %1 Box dimensions must be greater than zero.")
                          .arg (QString::fromStdString (collision.name));
        }
        if ((kind == GeometryKind::Cylinder || kind == GeometryKind::Sphere ||
             kind == GeometryKind::Cone) && !(collision.radius > 0)) {
            errors << QString ("CollisionModel %1 radius must be greater than zero.")
                          .arg (QString::fromStdString (collision.name));
        }
        if ((kind == GeometryKind::Cylinder || kind == GeometryKind::Cone) &&
            !(collision.length > 0)) {
            errors << QString ("CollisionModel %1 length must be greater than zero.")
                          .arg (QString::fromStdString (collision.name));
        }
        if ((kind == GeometryKind::Mesh || kind == GeometryKind::Polytope) &&
            isEmpty (collision.filePath)) {
            errors << QString ("CollisionModel %1 file path is required for %2 geometry.")
                          .arg (QString::fromStdString (collision.name),
                                QString::fromStdString (collision.shape));
        }
        for (double value : collision.dimensions) {
            if (!std::isfinite (value))
                errors << QString ("CollisionModel %1 dimensions must be finite.")
                              .arg (QString::fromStdString (collision.name));
        }
        if (!std::isfinite (collision.radius) || !std::isfinite (collision.length))
            errors << QString ("CollisionModel %1 radius/length must be finite.")
                          .arg (QString::fromStdString (collision.name));
        for (double value : collision.rpyDeg) {
            if (!std::isfinite (value))
                errors << QString ("CollisionModel %1 RPY values must be finite.")
                              .arg (QString::fromStdString (collision.name));
        }
        for (double value : collision.pos) {
            if (!std::isfinite (value))
                errors << QString ("CollisionModel %1 Pos values must be finite.")
                              .arg (QString::fromStdString (collision.name));
        }
    }

    // ---- 关节限位:min<max、速度/加速度>0、jointName 必须是"可动关节"(Revolute/Prismatic)----
    for (const JointLimitSpec& limit : spec.limits) {
        if (isEmpty (limit.jointName))
            errors << "Limit rows require a joint name.";
        else if (movableNames.find (limit.jointName) == movableNames.end ())
            errors << QString ("Limit references non-movable joint %1 "
                               "(only Revolute/Prismatic accept limits).")
                          .arg (QString::fromStdString (limit.jointName));
        if (!std::isfinite (limit.posMin) || !std::isfinite (limit.posMax))
            errors << QString ("Limit %1 posMin/posMax must be finite.")
                          .arg (QString::fromStdString (limit.jointName));
        if (limit.posMin >= limit.posMax)
            errors << QString ("Position min must be less than max for %1.")
                          .arg (QString::fromStdString (limit.jointName));
        if (limit.velMax <= 0)
            errors << QString ("Velocity limit must be greater than zero for %1.")
                          .arg (QString::fromStdString (limit.jointName));
        if (limit.accMax <= 0)
            errors << QString ("Acceleration limit must be greater than zero for %1.")
                          .arg (QString::fromStdString (limit.jointName));
    }

    // ---- 位姿:q 长度必须 == 可动关节数 ----
    const size_t expectedQ = movable.size ();
    for (const PoseSpec& pose : spec.poses) {
        if (isEmpty (pose.name))
            errors << "Pose names must not be empty.";
        if (pose.q.size () != expectedQ) {
            errors << QString ("Pose '%1' has %2 q-values but the robot has %3 movable "
                               "joints (Revolute/Prismatic).")
                          .arg (QString::fromStdString (pose.name))
                          .arg (static_cast< int >(pose.q.size ()))
                          .arg (static_cast< int >(expectedQ));
        }
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
            else if (movableNames.find (link.objectName) == movableNames.end ())
                errors << QString ("Dynamics: link object '%1' is not a movable joint "
                                   "(only Revolute/Prismatic accept Link).")
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
            else if (movableNames.find (fl.jointName) == movableNames.end ())
                errors << QString ("Dynamics: ForceLimit references non-movable joint %1.")
                              .arg (QString::fromStdString (fl.jointName));
            if (fl.maxForce <= 0)
                errors << QString ("Dynamics: ForceLimit for %1 must be greater than zero.")
                              .arg (QString::fromStdString (fl.jointName));
        }
    }

    // =====================================================================
    //  Milestone 6:CollisionSetup / ProximitySetup / Scene <Include> 校验
    //  已知 frame 名集合(用于校验 exclude pair、volatile frame 等):
    //    * transformJoints 名;Base;TCP;
    //    * robotBaseFrame 名(若 generateScene 通常是 RobotBase);
    //    * sceneFrames 名。
    //  注意:RobWork 的 Drawable/CollisionModel 会把 geometry 挂到 refframe,
    //  CollisionSetup/ProximitySetup 过滤的是 frame pair,不是几何/模型 name。
    //  校验要点:
    //    1) collisionSetup.enabled → file 非空;
    //    2) proximitySetup.enabled → file 非空;
    //    3) excludePairs 的 first/second 非空且必须引用已知 frame;
    //    4) volatileFrames 非空且必须引用已知 frame;
    //    5) proximitySetup.rules 的 patternA/patternB 非空;
    //    6) includes 项的文件名非空;
    // =====================================================================
    {
        std::set< std::string > collisionKnown;
        for (const std::string& n : allNames)
            collisionKnown.insert (n);
        collisionKnown.insert ("Base");
        collisionKnown.insert ("TCP");
        if (!spec.robotBaseFrame.name.empty ())
            collisionKnown.insert (spec.robotBaseFrame.name);
        if (spec.generateScene) {
            collisionKnown.insert ("WORLD");
            collisionKnown.insert ("RobotBase");
            for (const FrameSpec& frame : spec.sceneFrames)
                if (!frame.name.empty ())
                    collisionKnown.insert (frame.name);
        }

        // CollisionSetup
        if (spec.collisionSetup.enabled) {
            if (isEmpty (spec.collisionSetup.file))
                errors << "CollisionSetup file must not be empty when collision setup is enabled.";

            for (const FramePairSpec& pair : spec.collisionSetup.excludePairs) {
                if (isEmpty (pair.first) || isEmpty (pair.second)) {
                    errors << QString ("CollisionSetup exclude pair requires both first "
                                       "and second frame names.")
                                  .arg (QString::fromStdString (pair.first))
                                  .arg (QString::fromStdString (pair.second));
                    continue;
                }
                if (collisionKnown.find (pair.first) == collisionKnown.end ())
                    errors << QString ("CollisionSetup exclude pair references unknown "
                                       "frame: %1")
                                  .arg (QString::fromStdString (pair.first));
                if (collisionKnown.find (pair.second) == collisionKnown.end ())
                    errors << QString ("CollisionSetup exclude pair references unknown "
                                       "frame: %1")
                                  .arg (QString::fromStdString (pair.second));
            }

            for (const std::string& frame : spec.collisionSetup.volatileFrames) {
                if (frame.empty ()) {
                    errors << "CollisionSetup volatile frame name must not be empty.";
                    continue;
                }
                if (collisionKnown.find (frame) == collisionKnown.end ())
                    errors << QString ("CollisionSetup volatile frame references unknown "
                                       "frame: %1")
                                  .arg (QString::fromStdString (frame));
            }
        }

        // ProximitySetup
        if (spec.proximitySetup.enabled) {
            if (isEmpty (spec.proximitySetup.file))
                errors << "ProximitySetup file must not be empty when proximity setup is enabled.";

            for (const ProximityRuleSpec& rule : spec.proximitySetup.rules) {
                const bool aEmpty = isEmpty (rule.patternA);
                const bool bEmpty = isEmpty (rule.patternB);
                if (aEmpty || bEmpty)
                    errors << QString ("ProximitySetup rule %1 PatternA/PatternB must not be empty.")
                                  .arg (rule.kind == ProximityRuleKind::Include ? "Include" : "Exclude");
            }
        }

        // 用户自定义 Scene 引用列表
        for (const IncludeSpec& include : spec.includes) {
            if (isEmpty (include.file)) {
                errors << "Scene <Include> file path must not be empty.";
                continue;
            }
            if (spec.generateScene) {
                const QString rawPath = QString::fromStdString (include.file).trimmed ();
                QFileInfo includeInfo (rawPath);
                const QString resolvedPath = includeInfo.isAbsolute ()
                    ? includeInfo.absoluteFilePath ()
                    : QDir (QString::fromStdString (spec.saveDirectory)).filePath (rawPath);
                if (!QFileInfo::exists (resolvedPath)) {
                    errors << QString ("Scene reference file does not exist: %1")
                                  .arg (QDir::fromNativeSeparators (rawPath));
                }
            }
        }

        // Scene 启用但 collision/proximity 关闭时也允许(用户可以单独不要碰撞),
        // 不强加报错;留给上层 UI 决定是否提示。
    }

    return errors.isEmpty ();
}

/**
 * @brief 检查是否满足导出高级 <DHJoint> (Denavit-Hartenberg 关节) 的前置条件。
 * 
 * @details 该函数作为控制高级 DH 参数导出的“安全闸门”。必须同时满足以下所有条件才能放行：
 *          1. 至少包含一行 SE(3) 关节数据（必须有变换矩阵真值才可以进行投影）；
 *          2. SE(3) 关节行数与 DH 投影视图的行数保持绝对一致（用于 UI 和数据的防御性同步校验）；
 *          3. 遍历每一行关节，其类型 (type) 必须严格为 Revolute（旋转关节，简化 DH 模型暂不支持移动关节等其他类型）；
 *          4. 每一行从 SE(3) 到 DH 的投影转换必须是无损的（即 pitch = 0 且 roll 方向与 pos.xy 一致，详见 transformJointToDh 实现）。
 * 
 * @param spec   机器人模型配置规范数据对象 (RobotModelSpec)。
 * @param errors [out] 错误信息收集列表指针 (QStringList*)。
 *                     若传入非空指针，函数会将检查过程中发现的所有失败原因依次写入该列表中。
 * 
 * @return bool 如果完全满足导出条件返回 true（即 spec.exportDhJointsAdvanced = true 的唯一放行标准）；
 *              只要有任何一项校验不通过，均返回 false。
 */
bool RobotModelXmlWriter::canExportDhJoints (const RobotModelSpec& spec, QStringList* errors)
{
    // 创建局部错误列表，防止外部传入的 errors 为 nullptr 时导致程序崩溃
    QStringList localErrors;
    // 如果外部传入了有效的 errors 指针，则使用外部指针；否则引用局部的 localErrors（安全兜底）
    QStringList& out = errors == nullptr ? localErrors : *errors;

    // 检查 1：确保至少存在一个 SE(3) 关节数据，否则无法进行 DH 参数投影
    if (spec.transformJoints.empty ()) {
        out << "Advanced DH export requires at least one SE(3) joint row.";
        return false;
    }

    // 检查 2：防御性校验，确保 SE(3) 关节行数与 DH 关节行数严格对应一致
    if (spec.transformJoints.size () != spec.dhJoints.size ()) {
        out << "Advanced DH export requires DH projection rows to match SE(3) rows.";
        return false;
    }

    bool ok = true;

    // 逐行遍历所有 SE(3) 关节，进行类型检查与投影无损校验
    for (size_t i = 0; i < spec.transformJoints.size (); ++i) {
        const JointTransformSpec& joint = spec.transformJoints[i];

        // 检查 3：校验关节类型是否为旋转关节 (Revolute)
        if (!isRevoluteType (joint.type)) {
            out << QString ("Advanced DH export only supports Revolute rows; row %1 (%2) is %3.")
                       .arg (static_cast< int > (i + 1))               // 行号（1-indexed，便于用户查看）
                       .arg (QString::fromStdString (joint.name))      // 关节名称
                       .arg (QString::fromStdString (joint.type));     // 当前实际类型
            ok = false;
            continue; // 类型的非旋转关节无需再校验 DH 投影，直接跳转至下一行
        }

        // 检查 4：计算 SE(3) -> DH 的投影，检查转换过程中是否存在数据/自由度损失
        bool lossy = false;
        transformJointToDh (joint, &lossy);

        if (lossy) {
            out << QString ("Advanced DH export requires lossless SE(3)->DH projection; row %1 (%2) is lossy.")
                       .arg (static_cast< int > (i + 1))
                       .arg (QString::fromStdString (joint.name));
            ok = false; // 标记存在有损转换，但不中断循环，以便收集完所有有损行数的信息
        }
    }

    // 只有所有行均校验通过，ok 才为 true
    return ok;
}
// =============================================================================
//  makeSerialDeviceXml
//  说明: 将 RobotModelSpec 序列化为 RobWork 格式的串行设备 XML 字符串
//        (<SerialDevice name="...">...</SerialDevice>)。
//
//  生成的节点结构顺序:
//    1. 基座节点 (<Frame name="Base">)
//    2. 运动学关节节点 (高级 DH 导出分支 <DHJoint> 或 SE(3) 分支 <Joint>/<Frame>)
//    3. 末端工具坐标系 (<Frame name="TCP">)
//    4. 可视化几何体 (<Drawable>) [可选]
//    5. 独立碰撞模型 (<CollisionModel>)
//    6. 运动学限位 (<PosLimit> / <VelLimit> / <AccLimit>)
//    7. 预设位姿 configuration 向量 (<Q>)
// =============================================================================
QString RobotModelXmlWriter::makeSerialDeviceXml (const RobotModelSpec& spec)
{
    QString xml;
    QTextStream out (&xml);

    // 1. 写入 SerialDevice 根节点与默认 Base 基座坐标系
    out << "<SerialDevice name=\"" << xmlEscaped (exportedRobotName (spec)) << "\">\n";
    out << "  <Frame name=\"Base\">\n";
    out << "    <RPY>0 0 0</RPY>\n";
    out << "    <Pos>0 0 0</Pos>\n";
    if (spec.showFrameAxes)
        out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
    out << "  </Frame>\n";

    // 2. 判断是否满足高级 DH 参数导出条件（闸门拦截 + 用户开关）
    const bool writeDhJoints = spec.exportDhJointsAdvanced && canExportDhJoints (spec);

    if (writeDhJoints) {
        // ---------------------------------------------------------------------
        // 分支 A: 导出 DH 关节参数节点 (<DHJoint>)
        // 说明: 将 SE(3) 真值转换为标准 Schilling 约定的 DH 参数输出
        // ---------------------------------------------------------------------
        for (const JointTransformSpec& transformJoint : spec.transformJoints) {
            const DHJointSpec joint = transformJointToDh (transformJoint);
            out << "  <DHJoint name=\"" << xmlEscaped (joint.name) << "\" alpha=\""
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
        // ---------------------------------------------------------------------
        // 分支 B: 导出 SE(3) 矩阵参数节点 (默认路径: <Joint> 与 <Frame>)
        // 说明: 根据关节类型区分输出 <Frame> (固定/工具系) 或 <Joint> (可动关节)
        // ---------------------------------------------------------------------
        for (size_t i = 0; i < spec.transformJoints.size (); ++i) {
            const JointTransformSpec& joint = spec.transformJoints[i];

            if (isFixedFrameType (joint.type) || isToolFrameType (joint.type)) {
                // FixedFrame / ToolFrame 不计入可动关节，作为坐标系 <Frame> 输出
                // 父坐标系 (refframe) 自动向上一级推导；若为首节点则默认挂载至 Base
                const QString refframe = i > 0
                    ? QString::fromStdString (spec.transformJoints[i - 1].name)
                    : "Base";

                out << "  <Frame name=\"" << xmlEscaped (joint.name)
                    << "\" refframe=\"" << xmlEscaped (refframe) << "\">\n";
                out << "    <RPY>" << vector3 (joint.rpyDeg) << "</RPY>\n";
                out << "    <Pos>" << vector3 (joint.pos) << "</Pos>\n";
                if (spec.showFrameAxes)
                    out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
                out << "  </Frame>\n";
            }
            else {
                // 运动学可动关节 (Revolute / Prismatic)，输出 <Joint> 节点
                out << "  <Joint name=\"" << xmlEscaped (joint.name) << "\" type=\""
                    << xmlEscaped (joint.type) << "\">\n";
                out << "    <RPY>" << vector3 (joint.rpyDeg) << "</RPY>\n";
                out << "    <Pos>" << vector3 (joint.pos) << "</Pos>\n";
                if (spec.showFrameAxes)
                    out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
                out << "  </Joint>\n";
            }
        }
    }

    // 3. 动态配置末端工具坐标系 (TCP)
    // 说明: TCP 默认挂载于运动链中最后一个“可动关节”上；若无可动关节则挂载至 Base
    QString tcpRef = "Base";
    const std::vector< size_t > movable = movableJointIndices (spec);
    if (!movable.empty ())
        tcpRef = QString::fromStdString (spec.transformJoints[movable.back ()].name);

    out << "  <Frame name=\"TCP\" refframe=\"" << xmlEscaped (tcpRef) << "\">\n";
    out << "    <RPY>0 0 0</RPY>\n";
    out << "    <Pos>0 0 0</Pos>\n";
    if (spec.showFrameAxes)
        out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
    out << "  </Frame>\n";

    // 4. 写入视觉几何绘制实体 (<Drawable>)
    if (spec.generateDrawables) {
        for (const DrawableSpec& drawable : spec.drawables)
            writeDrawableXml (out, spec, drawable);
    }

    // 5. 写入独立碰撞模型 (<CollisionModel>)
    // 说明: 不受 generateDrawables 开关控制，即使关闭视觉渲染也可独立输出碰撞体
    for (const CollisionModelSpec& collision : spec.collisionModels)
        writeCollisionModelXml (out, spec, collision);

    // 6. 写入关节运动限位属性 (位置/角度限位、速度限位、加速度限位)
    for (const JointLimitSpec& limit : spec.limits) {
        out << "  <PosLimit refjoint=\"" << xmlEscaped (limit.jointName)
            << "\" min=\"" << number (limit.posMin) << "\" max=\""
            << number (limit.posMax) << "\" />\n";
    }
    for (const JointLimitSpec& limit : spec.limits) {
        out << "  <VelLimit refjoint=\"" << xmlEscaped (limit.jointName)
            << "\" max=\"" << number (limit.velMax) << "\" />\n";
    }
    for (const JointLimitSpec& limit : spec.limits) {
        out << "  <AccLimit refjoint=\"" << xmlEscaped (limit.jointName)
            << "\" max=\"" << number (limit.accMax) << "\" />\n";
    }

    // 7. 写入预设关节构型向量 (<Q>)
    // 说明: q 向量仅包含“可动关节”。若关节为 Revolute (旋转关节)，写入 XML 前需将度 (deg) 自动转换为弧度 (rad)
    for (const PoseSpec& pose : spec.poses) {
        out << "  <Q name=\"" << xmlEscaped (pose.name) << "\">";
        for (size_t k = 0; k < movable.size (); ++k) {
            if (k > 0)
                out << " ";
            const JointTransformSpec& joint = spec.transformJoints[movable[k]];
            const double raw = pose.q[k];
            out << number (isRevoluteType (joint.type) ? degToRad (raw) : raw);
        }
        out << "</Q>\n";
    }

    // 8. 闭合根节点并返回生成的 XML 字符串
    out << "</SerialDevice>\n";
    return xml;
}
/**
 * @brief 生成 RobWork 场景容器 (WorkCell) 的 XML 配置字符串。
 * 
 * @details 该函数负责构建 RobWork 仿真框架中的场景主文件（通常为 scene.wc.xml）：
 *          - 创建根节点 <WorkCell name="<robotName>Scene">。
 *          - 输出机器人基座坐标系 Frame（RobotBase），提供系统的默认兜底位姿（WORLD, 0 0 0）。
 *          - 紧跟 RobotBase 写入主机器人的 <Include> 节点。注意：RobWork 在解析时，若被包含的 Device 
 *            未显式指定 refframe，默认会将其挂载到 WorkCell 中当前定义的最后一个 Frame 上，因此顺序极其关键。
 *          - 顺序配置碰撞检测 (CollisionSetup) 与邻近度检测 (ProximitySetup) 文件引用（相对路径导出）。
 *          - 处理用户自定义追加的额外 <Include> 节点。
 *          - 依次写入场景中的其他静态/动态坐标系 (sceneFrames，如 Table / Workpiece / CameraFrame) 
 *            及场景几何体 (sceneGeometries)。
 * 
 * @param spec 机器人模型配置规范数据对象 (RobotModelSpec)，包含场景坐标系、几何体及外部包含文件等信息。
 * 
 * @return QString 格式化后的完整 WorkCell XML 文本。
 */
QString RobotModelXmlWriter::makeSceneXml (const RobotModelSpec& spec)
{
    // 获取导出的机器人名称，用于构建 Scene 名称及包含的 `.wc.xml` 文件名
    const QString robotName = exportedRobotName (spec);
    QString xml;
    QTextStream out (&xml);

    // 1. 写入根节点 <WorkCell>
    out << "<WorkCell name=\"" << xmlEscaped (robotName) << "Scene\">\n";

    // 2. 准备并写入 RobotBase 坐标系 (Frame)
    FrameSpec robotBase = spec.robotBaseFrame;
    // 名称兜底：若未指定，默认为 "RobotBase"
    if (robotBase.name.empty ())
        robotBase.name = "RobotBase";
    // 参考坐标系兜底：若未指定，默认挂在全局原点 "WORLD"
    if (robotBase.refFrame.empty ())
        robotBase.refFrame = "WORLD";
    // 强制指定基座坐标系类型为固定坐标系 (Fixed)
    robotBase.frameType = SceneFrameType::Fixed;
    
    // 写入 RobotBase 的 XML 节点
    writeFrameXml (out, robotBase, spec.showFrameAxes);
    out << "\n";

    // 3. 立即包含机器人设备文件
    // RobWork 特性：未显式指定 refframe 的设备会自动附着到上文最近定义的坐标系（即 RobotBase）
    out << "  <Include file=\"" << xmlEscaped (robotName + ".wc.xml") << "\" />\n";

    // 4. Milestone 6 特性：配置碰撞检测 (CollisionSetup) 与邻近度检测 (ProximitySetup) 引用
    // 所有路径均转换为相对路径，避免导出文件对特定机器绝对路径的依赖
    if (spec.collisionSetup.enabled) {
        const QString setupPath = collisionSetupFilePath (spec);
        const QString rel       = relativeOutputPath (spec, setupPath);
        out << "  <CollisionSetup file=\"" << xmlEscaped (rel) << "\" />\n";
    }
    if (spec.proximitySetup.enabled) {
        const QString setupPath = proximitySetupFilePath (spec);
        const QString rel       = relativeOutputPath (spec, setupPath);
        out << "  <ProximitySetup file=\"" << xmlEscaped (rel) << "\" />\n";
    }

    // 5. 遍历处理用户在 spec.includes 中显式追加的额外引用文件（如第三方设备、额外碰撞文件等）
    for (const IncludeSpec& include : spec.includes) {
        if (!include.file.empty ()) {
            const QString rel = relativeOutputPath (spec, QString::fromStdString (include.file));
            // 根据不同的引用类型映射为对应的 XML 节点标签
            switch (include.kind) {
                case IncludeKind::Collision:
                    out << "  <CollisionSetup file=\"" << xmlEscaped (rel) << "\" />\n";
                    break;
                case IncludeKind::Proximity:
                    out << "  <ProximitySetup file=\"" << xmlEscaped (rel) << "\" />\n";
                    break;
                case IncludeKind::Device:
                case IncludeKind::WorkCell:
                default:
                    out << "  <Include file=\"" << xmlEscaped (rel) << "\" />\n";
                    break;
            }
        }
    }
    out << "\n";

    // 6. 依次写入场景中的所有坐标系 (sceneFrames，如工作台 Table、工件 Workpiece 等)
    for (const FrameSpec& frame : spec.sceneFrames)
        writeFrameXml (out, frame, spec.showFrameAxes);
    if (!spec.sceneFrames.empty ())
        out << "\n";

    // 7. 依次写入场景中的所有几何体模型 (sceneGeometries)
    for (const SceneGeometrySpec& geometry : spec.sceneGeometries)
        writeSceneGeometryXml (out, geometry);
    if (!spec.sceneGeometries.empty ())
        out << "\n";

    // 8. 闭合 <WorkCell> 根节点并返回 XML 字符串
    out << "</WorkCell>\n";
    return xml;
}

// =============================================================================
//  makeDynamicWorkCellXml
//  说明: 生成 RobWorkSim 用的 DynamicWorkCell XML:
//        - <DynamicWorkCell workcell="..."> 根节点
//        - <RigidDevice> 内含所有 <ForceLimit> + <KinematicBase> + <Link>...
//        - <Link> 中,estimateInertia=true 用 <EstimateInertia />,否则输出 9 个
//          数字的 3x3 惯量矩阵(行优先展开)
// =============================================================================
QString RobotModelXmlWriter::makeDynamicWorkCellXml (const RobotModelSpec& spec)
{
    const QString robotName = exportedRobotName (spec);
    QString xml;
    QTextStream out (&xml);
    out << "<DynamicWorkCell workcell=\"" << xmlEscaped (robotName + "Scene.wc.xml") << "\">\n";
    out << "  <RigidDevice device=\"" << xmlEscaped (robotName) << "\">\n";

    for (const JointForceLimitSpec& fl : spec.dynamics.forceLimits) {
        out << "    <ForceLimit joint=\"" << xmlEscaped (fl.jointName) << "\">"
            << number (fl.maxForce) << "</ForceLimit>\n";
    }

    out << "    <KinematicBase frame=\"" << xmlEscaped (spec.dynamics.baseFrame)
        << "\">\n";
    out << "      <MaterialID>" << xmlEscaped (spec.dynamics.baseMaterial)
        << "</MaterialID>\n";
    out << "    </KinematicBase>\n";

    for (const LinkDynamicsSpec& link : spec.dynamics.links) {
        out << "    <Link object=\"" << xmlEscaped (link.objectName) << "\">\n";
        out << "      <Mass>" << number (link.mass) << "</Mass>\n";
        out << "      <COG>" << vector3 (link.cog) << "</COG>\n";
        if (link.estimateInertia) {
            out << "      <EstimateInertia />\n";
        }
        else {
            // RobWorkSim 只接受 3 或 9 个数;这里把 6 元惯量数据
            // (Ixx, Iyy, Izz, Ixy, Ixz, Iyz) 展开成行优先 3x3 矩阵:
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
        out << "      <MaterialID>" << xmlEscaped (link.material) << "</MaterialID>\n";
        out << "    </Link>\n";
    }

    out << "  </RigidDevice>\n";
    out << "</DynamicWorkCell>\n";
    return xml;
}

/**
 * @brief 生成 RobWork 碰撞检测配置文件 (<CollisionSetup>) 的 XML 字符串。
 * 
 * @details 该函数依据规范生成 RobWork Loader 识别的碰撞组态 XML：
 *          - **排除碰撞对 (<Exclude>)**：写入无需进行碰撞检测的 Frame 配对，格式为 `<FramePair first="..." second="..."/>`。
 *            数据源包含两部分（由 effectiveCollisionExcludePairs 内部合并并按序去重）：
 *              1) 用户显式配置的配对 (spec.collisionSetup.excludePairs)；
 *              2) 开启 excludeAdjacentLinkPairs 时，自动生成的相邻连杆/关节配对 (Joint{i} - Joint{i+1})。
 *          - **易变/动态坐标系 (<Volatile>)**：写入在运行过程中位姿频繁变动或不参与静态层次优化的 Frame 名称；若列表为空则自动忽略该段落。
 *          - **静态碰撞排除标签 (<ExcludeStaticPairs/>)**：当标志位 excludeStaticPairs 为 true 时输出，指示仿真器忽略所有静态环境/连杆之间的碰撞计算。
 * 
 * @param spec 机器人模型配置规范数据对象 (RobotModelSpec)。
 * 
 * @return QString 格式化后的完整 <CollisionSetup> XML 文本。
 */
QString RobotModelXmlWriter::makeCollisionSetupXml (const RobotModelSpec& spec)
{
    // 获取经过合并与去重后的“有效碰撞排除配对列表”
    // (优先保留用户配置配对，后追加自动生成的相邻连杆配对，自动跳过空名称)
    const std::vector< FramePairSpec > pairs = effectiveCollisionExcludePairs (spec);
    QString xml;
    QTextStream out (&xml);

    // 1. 写入根节点 <CollisionSetup>
    out << "<CollisionSetup>\n";

    // 2. 如果存在需要排除碰撞的 Frame 配对，写入 <Exclude> 节点块
    if (!pairs.empty ()) {
        out << "  <Exclude>\n";
        for (const FramePairSpec& pair : pairs) {
            out << "    <FramePair first=\"" << xmlEscaped (pair.first)
                << "\" second=\"" << xmlEscaped (pair.second) << "\"/>\n";
        }
        out << "  </Exclude>\n";
    }

    // 3. 遍历并写入所有指定的易变坐标系 (<Volatile>)
    // 在 RobWork 中，标记为 Volatile 的 Frame 通常代表频繁变化或需要特别对待的碰撞对象
    for (const std::string& frame : spec.collisionSetup.volatileFrames) {
        if (frame.empty ())
            continue; // 跳过无效的空名称
        out << "  <Volatile>" << xmlEscaped (frame) << "</Volatile>\n";
    }

    // 4. 若开启了全局静态对碰撞排除，写入空标签 <ExcludeStaticPairs/>
    if (spec.collisionSetup.excludeStaticPairs)
        out << "  <ExcludeStaticPairs/>\n";

    // 5. 闭合根节点并返回 XML 文本
    out << "</CollisionSetup>\n";
    return xml;
}

// =============================================================================
//  makeProximitySetupXml
//  说明: 输出 RobWork <ProximitySetup> 结构:
//        <ProximitySetup UseIncludeAll=".." UseExcludeStaticPairs="..">
//          <Include PatternA=".." PatternB=".."/>
//          <Exclude PatternA=".." PatternB=".."/>
//        </ProximitySetup>
//        空 rules 时仅输出根 + 两个开关属性(Use* 全 false 也照常输出)。
// =============================================================================
QString RobotModelXmlWriter::makeProximitySetupXml (const RobotModelSpec& spec)
{
    QString xml;
    QTextStream out (&xml);
    out << "<ProximitySetup UseIncludeAll=\""
        << (spec.proximitySetup.useIncludeAll ? "true" : "false")
        << "\" UseExcludeStaticPairs=\""
        << (spec.proximitySetup.useExcludeStaticPairs ? "true" : "false")
        << "\">\n";

    for (const ProximityRuleSpec& rule : spec.proximitySetup.rules) {
        out << "  <" << (rule.kind == ProximityRuleKind::Include ? "Include" : "Exclude")
            << " PatternA=\"" << xmlEscaped (rule.patternA)
            << "\" PatternB=\"" << xmlEscaped (rule.patternB)
            << "\"/>\n";
    }

    out << "</ProximitySetup>\n";
    return xml;
}

// =============================================================================
//  三个文件路径辅助函数 + Milestone 6 setup 路径:saveDirectory / robotName + 后缀
// =============================================================================
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

// Milestone 6:CollisionSetup/ProximitySetup 落盘路径使用用户在 spec 里配置
// 的文件名;空字符串兜底默认文件名,以保证 Scene XML 引用与真实文件一致。
QString RobotModelXmlWriter::collisionSetupFilePath (const RobotModelSpec& spec)
{
    QDir dir (QString::fromStdString (spec.saveDirectory));
    const QString file = QString::fromStdString (spec.collisionSetup.file).trimmed ();
    return dir.filePath (file.isEmpty () ? QString ("CollisionSetup.xml") : file);
}

QString RobotModelXmlWriter::proximitySetupFilePath (const RobotModelSpec& spec)
{
    QDir dir (QString::fromStdString (spec.saveDirectory));
    const QString file = QString::fromStdString (spec.proximitySetup.file).trimmed ();
    return dir.filePath (file.isEmpty () ? QString ("ProximitySetup.xml") : file);
}

// Milestone 6:把所有 Scene 引用的文件路径转成相对 saveDirectory 的相对路径,
// 避免 XML 输出依赖绝对路径。已经是相对路径的原文返回。
QString RobotModelXmlWriter::relativeOutputPath (const RobotModelSpec& spec,
                                                 const QString& filePath)
{
    const QString trimmed = filePath.trimmed ();
    if (trimmed.isEmpty ())
        return trimmed;
    QFileInfo info (trimmed);
    if (!info.isAbsolute ())
        return QDir::fromNativeSeparators (trimmed);
    QDir outDir (QString::fromStdString (spec.saveDirectory));
    return QDir::fromNativeSeparators (outDir.relativeFilePath (info.absoluteFilePath ()));
}

/**
 * @brief 计算并获取最终有效的碰撞排除配对列表 (Collision Exclude Frame Pairs)。
 * 
 * @details 该函数负责将用户手动配置的排除配对与系统自动生成的相邻关节/连杆配对进行合并：
 *          1. **合法性过滤**：自动跳过包含空名称的 Frame、以及自我配对（first == second）；
 *          2. **去重机制**：基于 `first|second` 格式的字符串作为 Key 建立 `std::set` 集合，实现高效率去重；
 *          3. **合并顺序**：优先保留用户手动配置的配对 (`spec.collisionSetup.excludePairs`)，
 *             而后在开启 `excludeAdjacentLinkPairs` 时追加相邻关节配对 (`Joint{i}` -> `Joint{i+1}`)；
 *          4. **相邻匹配**：只有当 `transformJoints` 列表中至少存在两个关节，且相邻两端的名称均非空时才会加入。
 * 
 * @param spec 机器人模型配置规范数据对象 (RobotModelSpec)。
 * 
 * @return std::vector<FramePairSpec> 去重且合并后的有效碰撞排除配对列表。
 */
std::vector< FramePairSpec >
RobotModelXmlWriter::effectiveCollisionExcludePairs (const RobotModelSpec& spec)
{
    std::vector< FramePairSpec > result; // 存储最终合并后的配对结果
    std::set< std::string > seen;       // 用于记录已处理过的配对 Key，实现高效去重

    // Lambda 表达式：封装通用的合法性检查、去重及压栈逻辑
    auto push = [&] (const std::string& first, const std::string& second) {
        // 过滤条件 1：任意一端名称为空则属于无效配对，直接跳过
        if (first.empty () || second.empty ())
            return;
        // 过滤条件 2：相同坐标系自身的碰撞排除没有意义，跳过
        if (first == second)
            return;

        // 拼接唯一标识键 (Key)，例如 "Joint1|Joint2"
        const std::string key = first + "|" + second;
        
        // 过滤条件 3：尝试插入去重集合，若 key 已存在 (insert().second 为 false) 则直接跳过
        if (!seen.insert (key).second)
            return;

        // 构建配对结构体并存入结果集
        FramePairSpec pair;
        pair.first  = first;
        pair.second = second;
        result.push_back (pair);
    };

    // 步骤 1：优先将用户在配置中显式定义的排除配对压入列表
    for (const FramePairSpec& pair : spec.collisionSetup.excludePairs)
        push (pair.first, pair.second);

    // 步骤 2：若开启了“自动排除相邻连杆/关节碰撞”标志位，且至少存在 2 个关节，则自动追加相邻配对
    if (spec.collisionSetup.excludeAdjacentLinkPairs &&
        spec.transformJoints.size () >= 2) {
        
        // 遍历所有相邻关节对：(Joint_0, Joint_1), (Joint_1, Joint_2), ...
        for (size_t i = 0; i + 1 < spec.transformJoints.size (); ++i) {
            const std::string a = spec.transformJoints[i].name;
            const std::string b = spec.transformJoints[i + 1].name;
            
            // 按运动学链的顺向方向 (i -> i+1) 压入排除对
            push (a, b);
        }
    }

    // 返回合并去重后的完整排除列表
    return result;
}

// =============================================================================
//  saveFiles
//  说明: 先 validate,再按 spec.generateScene / spec.dynamics.generateDynamicWorkCell
//        决定写哪些文件;任何一步失败都会把错误追加到 errors 并返回 false。
//        Milestone 6:在 Scene XML 之前按 spec.collisionSetup.enabled /
//        spec.proximitySetup.enabled 写 CollisionSetup.xml / ProximitySetup.xml;
//        generateScene=false 时连带清理两个 setup 文件,避免遗留 stale 引用。
// =============================================================================
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
        // CollisionSetup 在 Scene 之前写:顺序 device -> collision -> proximity
        // -> scene,这样如果中途失败,Scene 的引用尚未生成。
        if (spec.collisionSetup.enabled) {
            QFile collisionFile (collisionSetupFilePath (spec));
            const QFileInfo collisionInfo (collisionFile.fileName ());
            if (!QDir ().mkpath (collisionInfo.absolutePath ())) {
                errors << QString ("Could not create directory %1").arg (collisionInfo.absolutePath ());
                return false;
            }
            if (!collisionFile.open (QFile::WriteOnly | QFile::Text)) {
                errors << QString ("Could not write %1").arg (collisionFile.fileName ());
                return false;
            }
            QTextStream collisionStream (&collisionFile);
            collisionStream << makeCollisionSetupXml (spec);
            collisionFile.close ();
        }
        else {
            QFile::remove (collisionSetupFilePath (spec));
        }

        if (spec.proximitySetup.enabled) {
            QFile proximityFile (proximitySetupFilePath (spec));
            const QFileInfo proximityInfo (proximityFile.fileName ());
            if (!QDir ().mkpath (proximityInfo.absolutePath ())) {
                errors << QString ("Could not create directory %1").arg (proximityInfo.absolutePath ());
                return false;
            }
            if (!proximityFile.open (QFile::WriteOnly | QFile::Text)) {
                errors << QString ("Could not write %1").arg (proximityFile.fileName ());
                return false;
            }
            QTextStream proximityStream (&proximityFile);
            proximityStream << makeProximitySetupXml (spec);
            proximityFile.close ();
        }
        else {
            QFile::remove (proximitySetupFilePath (spec));
        }

        QFile sceneFile (sceneFilePath (spec));
        if (!sceneFile.open (QFile::WriteOnly | QFile::Text)) {
            errors << QString ("Could not write %1").arg (sceneFile.fileName ());
            return false;
        }
        QTextStream sceneStream (&sceneFile);
        sceneStream << makeSceneXml (spec);
        sceneFile.close ();
    }
    else {
        // 生成 Scene 关闭时,连带删除 setup 文件,避免下一次开启 Scene 后
        // 这些 stale setup 仍留在 saveDirectory。
        QFile::remove (sceneFilePath (spec));
        QFile::remove (collisionSetupFilePath (spec));
        QFile::remove (proximitySetupFilePath (spec));
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

// =============================================================================
//  静态辅助:number / vector3 / degToRad
// =============================================================================

/// 浮点数统一格式:有效数字 15 位,精度与简短兼顾
QString RobotModelXmlWriter::number (double value)
{
    return QString::number (value, 'g', 15);
}

/// std::array<double,3> -> "x y z"(使用统一的 number 格式)
QString RobotModelXmlWriter::vector3 (const std::array< double, 3 >& values)
{
    return number (values[0]) + " " + number (values[1]) + " " + number (values[2]);
}

/// std::array<double,16> -> "m00 m01 ... m33"(行优先 4x4),用于 <Transform>
QString RobotModelXmlWriter::vector16 (const std::array< double, 16 >& values)
{
    QStringList parts;
    parts.reserve (16);
    for (double value : values)
        parts << number (value);
    return parts.join (" ");
}

/// 给出 <Frame> 的 type=... 属性串(Normal 时省略)
QString RobotModelXmlWriter::frameTypeAttribute (SceneFrameType type)
{
    if (type == SceneFrameType::Movable)
        return " type=\"Movable\"";
    if (type == SceneFrameType::Fixed)
        return " type=\"Fixed\"";
    return QString ();
}

/// 输出单个 <Frame ...>...</Frame>:
///   * Fixed / Movable 加 type 属性,Normal 时省略;
///   * daf=true 输出 daf="true";
///   * poseMode == Transform4x4 走 <Transform>,否则 <RPY>/<Pos>;
///   * showFrameAxes 时附带 ShowFrameAxis 属性。
void RobotModelXmlWriter::writeFrameXml (QTextStream& out, const FrameSpec& frame,
                                         bool showFrameAxes)
{
    out << "  <Frame name=\"" << xmlEscaped (frame.name)
        << "\" refframe=\"" << xmlEscaped (frame.refFrame) << "\""
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

/// Milestone 3.5:把 SceneGeometrySpec 序列化为对应形状的 XML 子节点。
/// 维度约定:
///   * Box   : size[0..2] -> x/y/z(全部 > 0)
///   * Plane : size[0..1] -> x/y(全部 > 0)
///   * Cylinder / Cone : radius(> 0), length=z(> 0)
///   * Sphere : radius(> 0)
///   * Mesh   : file(非空)
QString RobotModelXmlWriter::geometryShapeXml (const SceneGeometrySpec& geometry)
{
    switch (geometry.kind) {
        case GeometryKind::Cylinder:
            return QString ("<Cylinder radius=\"%1\" z=\"%2\" />")
                .arg (number (geometry.radius), number (geometry.length));
        case GeometryKind::Sphere:
            return QString ("<Sphere radius=\"%1\" />").arg (number (geometry.radius));
        case GeometryKind::Cone:
            return QString ("<Cone radius=\"%1\" z=\"%2\" />")
                .arg (number (geometry.radius), number (geometry.length));
        case GeometryKind::Plane:
            return QString ("<Plane x=\"%1\" y=\"%2\" />")
                .arg (number (geometry.size[0]), number (geometry.size[1]));
        case GeometryKind::STL:
            return QString ("<STL file=\"%1\" />")
                .arg (xmlEscaped (geometry.file));
        case GeometryKind::Mesh:
            return QString ("<Mesh file=\"%1\" />")
                .arg (xmlEscaped (geometry.file));
        case GeometryKind::Polytope:
            return QString ("<Polytope file=\"%1\" />")
                .arg (xmlEscaped (geometry.file));
        case GeometryKind::Box:
        default:
            return QString ("<Box x=\"%1\" y=\"%2\" z=\"%3\" />")
                .arg (number (geometry.size[0]), number (geometry.size[1]),
                      number (geometry.size[2]));
    }
}

/// Milestone 3.5:把 SceneGeometrySpec 输出为 <Drawable>:
///   * 始终输出 <RPY> / <Pos> / <RGB>;
///   * collisionModel=true → colmodel="Enabled";
///   * showFrameAxes 暂不适用(Milestone 3.5 不在 Drawable 上加 axes 属性)。
void RobotModelXmlWriter::writeSceneGeometryXml (QTextStream& out,
                                                 const SceneGeometrySpec& geometry)
{
    out << "  <Drawable name=\"" << xmlEscaped (geometry.name)
        << "\" refframe=\"" << xmlEscaped (geometry.refFrame) << "\"";
    if (geometry.collisionModel)
        out << " colmodel=\"Enabled\"";
    out << ">\n";
    out << "    <RPY>" << vector3 (geometry.rpyDeg) << "</RPY>\n";
    out << "    <Pos>" << vector3 (geometry.pos) << "</Pos>\n";
    out << "    <RGB>" << vector3 (geometry.rgb) << "</RGB>\n";
    out << "    " << geometryShapeXml (geometry) << "\n";
    out << "  </Drawable>\n";
}

/// Milestone 4:把文件几何路径转成相对 saveDirectory 的相对路径。
/// 已经是相对路径的原文返回;空字符串原样返回。绝对路径走
/// QDir::relativeFilePath,再用 "/" 统一分隔符。
QString RobotModelXmlWriter::relativeGeometryPath (const RobotModelSpec& spec,
                                                   const std::string& filePath)
{
    const QString raw = QString::fromStdString (filePath).trimmed ();
    if (raw.isEmpty ())
        return raw;
    QFileInfo info (raw);
    if (!info.isAbsolute ())
        return QDir::fromNativeSeparators (raw);
    QDir outDir (QString::fromStdString (spec.saveDirectory));
    return QDir::fromNativeSeparators (outDir.relativeFilePath (info.absoluteFilePath ()));
}

/// Milestone 4:把 DrawableSpec 序列化为对应形状的 XML 子节点。
/// 与 SceneGeometrySpec 共享 GeometryKind 枚举,但:
///   * 尺寸字段统一来自 drawable.dimensions(Box/Plane)或 drawable.radius/length;
///   * 文件几何(STL/Mesh/Polytope)走相对路径;
///   * Unknown 形状返回空串,T3 校验负责拦截。
QString RobotModelXmlWriter::drawableShapeXml (const RobotModelSpec& spec,
                                               const DrawableSpec& drawable)
{
    const GeometryKind kind = geometryKindFromString (drawable.shape);
    switch (kind) {
        case GeometryKind::Box:
            return QString ("<Box x=\"%1\" y=\"%2\" z=\"%3\" />")
                .arg (number (drawable.dimensions[0]),
                      number (drawable.dimensions[1]),
                      number (drawable.dimensions[2]));
        case GeometryKind::Cylinder:
            return QString ("<Cylinder radius=\"%1\" z=\"%2\" />")
                .arg (number (drawable.radius),
                      number (drawable.length));
        case GeometryKind::Sphere:
            return QString ("<Sphere radius=\"%1\" />")
                .arg (number (drawable.radius));
        case GeometryKind::Cone:
            return QString ("<Cone radius=\"%1\" z=\"%2\" />")
                .arg (number (drawable.radius),
                      number (drawable.length));
        case GeometryKind::Plane:
            return QString ("<Plane x=\"%1\" y=\"%2\" />")
                .arg (number (drawable.dimensions[0]),
                      number (drawable.dimensions[1]));
        case GeometryKind::Mesh:
        case GeometryKind::STL:
        case GeometryKind::Polytope:
            return QString ("<Polytope file=\"%1\" />")
                .arg (xmlEscaped (relativeGeometryPath (spec, drawable.filePath)));
        case GeometryKind::Unknown:
        default:
            return QString ();
    }
}

/// Milestone 4:把 DrawableSpec 输出为 <Drawable>(机器人本体几何体):
///   * 始终输出 <RPY>/<Pos>/<RGB>;
///   * collisionModel=true → colmodel="Enabled";
///   * 形状由 drawableShapeXml 决定(不再写死 Cylinder)。
void RobotModelXmlWriter::writeDrawableXml (QTextStream& out,
                                            const RobotModelSpec& spec,
                                            const DrawableSpec& drawable)
{
    out << "  <Drawable name=\"" << xmlEscaped (drawable.name)
        << "\" refframe=\"" << xmlEscaped (drawable.refFrame) << "\"";
    if (drawable.collisionModel)
        out << " colmodel=\"Enabled\"";
    out << ">\n";
    out << "    <RPY>" << vector3 (drawable.rpyDeg) << "</RPY>\n";
    out << "    <Pos>" << vector3 (drawable.pos) << "</Pos>\n";
    out << "    <RGB>" << vector3 (drawable.rgb) << "</RGB>\n";
    out << "    " << drawableShapeXml (spec, drawable) << "\n";
    out << "  </Drawable>\n";
}

// ============================================================================
//  独立碰撞模型(Milestone 5)
// ============================================================================
// CollisionModel 支持 6 种形状:Box / Cylinder / Sphere / Cone / Mesh / Polytope;
// Plane / STL / Unknown 在 Milestone 5 范围内被 validate 拒绝。
bool RobotModelXmlWriter::isCollisionModelShapeSupported (GeometryKind kind)
{
    return kind == GeometryKind::Box || kind == GeometryKind::Cylinder ||
           kind == GeometryKind::Sphere || kind == GeometryKind::Cone ||
           kind == GeometryKind::Mesh || kind == GeometryKind::Polytope;
}

// 把 CollisionModelSpec 序列化为对应形状 XML 子节点,与 drawableShapeXml
// 共享 relativeGeometryPath,但不输出 <RGB>(碰撞模型不是视觉对象)。
QString RobotModelXmlWriter::collisionShapeXml (const RobotModelSpec& spec,
                                                const CollisionModelSpec& collision)
{
    const GeometryKind kind = geometryKindFromString (collision.shape);
    switch (kind) {
        case GeometryKind::Box:
            return QString ("<Box x=\"%1\" y=\"%2\" z=\"%3\" />")
                .arg (number (collision.dimensions[0]),
                      number (collision.dimensions[1]),
                      number (collision.dimensions[2]));
        case GeometryKind::Cylinder:
            return QString ("<Cylinder radius=\"%1\" z=\"%2\" />")
                .arg (number (collision.radius), number (collision.length));
        case GeometryKind::Sphere:
            return QString ("<Sphere radius=\"%1\" />").arg (number (collision.radius));
        case GeometryKind::Cone:
            return QString ("<Cone radius=\"%1\" z=\"%2\" />")
                .arg (number (collision.radius), number (collision.length));
        case GeometryKind::Mesh:
        case GeometryKind::Polytope:
            return QString ("<Polytope file=\"%1\" />")
                .arg (xmlEscaped (relativeGeometryPath (spec, collision.filePath)));
        case GeometryKind::Plane:
        case GeometryKind::STL:
        case GeometryKind::Unknown:
        default:
            return QString ();
    }
}

// 把 CollisionModelSpec 输出为 <CollisionModel>:
//   * 不含 colmodel="Enabled" 属性(整段就是 collision);
//   * 不输出 <RGB>(非视觉);
//   * 不受 spec.generateDrawables 控制,独立循环。
void RobotModelXmlWriter::writeCollisionModelXml (QTextStream& out,
                                                  const RobotModelSpec& spec,
                                                  const CollisionModelSpec& collision)
{
    out << "  <CollisionModel name=\"" << xmlEscaped (collision.name)
        << "\" refframe=\"" << xmlEscaped (collision.refFrame) << "\">\n";
    out << "    <RPY>" << vector3 (collision.rpyDeg) << "</RPY>\n";
    out << "    <Pos>" << vector3 (collision.pos) << "</Pos>\n";
    out << "    " << collisionShapeXml (spec, collision) << "\n";
    out << "  </CollisionModel>\n";
}

/// 度 -> 弧度
double RobotModelXmlWriter::degToRad (double value)
{
    return value * Pi / 180.0;
}

// =============================================================================
//  对外暴露的 thin wrapper(实现委托给匿名命名空间中的函数)
// =============================================================================
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

int RobotModelXmlWriter::movableJointCount (const RobotModelSpec& spec)
{
    return static_cast< int >(::movableJointIndices (spec).size ());
}

void RobotModelXmlWriter::applyDefaultDrawables (RobotModelSpec& spec,
                                                 double /*paddingBeforeFirst*/)
{
    spec.drawables.erase (
        std::remove_if (spec.drawables.begin (), spec.drawables.end (),
                        [] (const DrawableSpec& d) {
                            const QString name = QString::fromStdString (d.name);
                            return name.endsWith ("Housing") ||
                                   QRegularExpression ("^Link\\d+To\\d+$").match (name).hasMatch ();
                        }),
        spec.drawables.end ());
    appendJointHousings (spec);
    appendLinks (spec);
    applyLinkGeometry (spec);
}

JointTransformSpec RobotModelXmlWriter::dhJointToTransform (const DHJointSpec& dh,
                                                            const std::string& existingType)
{
    JointTransformSpec j;
    j.name   = dh.name;
    j.type   = existingType.empty () ? std::string ("Revolute") : existingType;
    j.rpyDeg = {{dh.offsetDeg, 0.0, dh.alphaDeg}};
    const double theta = dh.offsetDeg * Pi / 180.0;
    j.pos = {{dh.a * std::cos (theta), dh.a * std::sin (theta), dh.d}};
    return j;
}
/**
 * @brief 将 SE(3) 空间关节变换 (JointTransformSpec) 投影/转换为 DH 参数 (DHJointSpec)。
 * 
 * @details 该函数负责将基于 Roll-Pitch-Yaw (RPY) 姿态和 3D 平移位置 (x, y, z) 表示的关节，
 *          投影计算为 Denavit-Hartenberg (DH) 4 参数模型 (a, alpha, d, offset)。
 *          由于简化 DH 参数的自由度受限（仅 4 个参数），无法完全拟合任意的 6-DOF SE(3) 变换，
 *          因此函数还会检测变换过程是否存在自由度或信息的丢失（lossy）：
 *          - **a (连杆长度)**: $x, y$ 平面上的欧氏距离 $\sqrt{x^2 + y^2}$。
 *          - **offsetDeg (关节角度偏移量)**: 当 $a > 0$ 时，为位置向量在 XY 平面上的方位角 $\text{atan2}(y, x)$；
 *            若 $a \approx 0$（无平面位移），则退化使用原始 RPY 的 Roll 角。
 *          - **d (连杆偏置)**: Z 轴方向的平移量 $z$。
 *          - **alphaDeg (连杆扭角)**: 姿态角中的 Yaw 角 ($rpy[2]$)。
 * 
 * @param joint 输入的 SE(3) 关节变换结构体，包含位置 (pos) 与 RPY 旋转角 (rpyDeg)。
 * @param lossy [out] 可选输出参数指针。若非空，将写入该 SE(3) -> DH 投影过程是否“有损”。
 *              满足以下任意条件即判定为有损 (lossy = true)：
 *              1. 关节类型为 Fixed 或 Tool（类型不匹配/非运动关节）；
 *              2. Pitch 角 ($rpy[1]$) 不为 0（简化 DH 假设无法表达 Pitch 倾角）；
 *              3. 位置向量在 XY 平面的方向角与旋转 Roll 角方向不一致。
 * 
 * @return DHJointSpec 转换后的 DH 关节参数对象。
 */
DHJointSpec RobotModelXmlWriter::transformJointToDh (const JointTransformSpec& joint, bool* lossy)
{
    DHJointSpec dh;
    dh.name = joint.name;

    // 1. 计算 DH 参数 a (连杆长度)：在机器人基坐标系 XY 平面上的投影距离
    dh.a = std::sqrt (joint.pos[0] * joint.pos[0] +
                       joint.pos[1] * joint.pos[1]);

    // 2. 计算 DH 参数 offsetDeg (关节角偏移量)：
    // 如果 XY 平面位移不为 0 (大于极小阈值 1e-12)，用 atan2 计算位置向量的倾角；
    // 否则（X、Y 均在原点处），直接沿用原变换中的 Roll 旋转角。
    dh.offsetDeg = dh.a > 1e-12 ? std::atan2 (joint.pos[1], joint.pos[0]) * 180.0 / Pi
                                 : joint.rpyDeg[0];

    // 3. 计算 DH 参数 d (连杆偏置)：沿 Z 轴的平移量
    dh.d = joint.pos[2];

    // 4. 计算 DH 参数 alphaDeg (连杆扭角)：绕 X 轴扭转的角度，映射自 RPY 中的 Yaw 角
    dh.alphaDeg = joint.rpyDeg[2];

    // 5. 校验转换是否发生“信息/自由度损失” (Lossy)
    if (lossy != nullptr) {
        // 校验 5.1: 类型有损——固定关节 (Fixed) 或末端工具关节 (Tool) 无法转换为标准的 DH 运动关节
        const bool typeLoss =
            isFixedFrameType (joint.type) || isToolFrameType (joint.type);

        // 校验 5.2: 俯仰角有损——Pitch 旋转角 ($rpy[1]$) 必须接近 0 (小于 1e-9)，否则 DH 参数无法表达该倾角
        const bool pitchLoss = std::abs (joint.rpyDeg[1]) > 1e-9;

        // 校验 5.3: 滚转与位置方向不匹配——当 $a > 0$ 时，Roll 角必须与 XY 平面位置向量的方向角保持一致；
        // 若 normalizedAngleDiffDeg 计算出的角度差大于 1e-6，则说明几何姿态与位置存在约束冲突。
        const bool rollPositionMismatch = dh.a > 1e-12 &&
            normalizedAngleDiffDeg (joint.rpyDeg[0], dh.offsetDeg) > 1e-6;

        // 任意一项不满足，即判定为“有损转换”
        *lossy = typeLoss || pitchLoss || rollPositionMismatch;
    }

    return dh;
}

void RobotModelXmlWriter::refreshDhProjectionFromTransform (RobotModelSpec& spec)
{
    const size_t n = std::min (spec.dhJoints.size (), spec.transformJoints.size ());
    for (size_t i = 0; i < n; ++i) {
        spec.dhJoints[i] = transformJointToDh (spec.transformJoints[i]);
    }
}

void RobotModelXmlWriter::applyDhInputToTransform (RobotModelSpec& spec)
{
    const size_t n = std::min (spec.dhJoints.size (), spec.transformJoints.size ());
    for (size_t i = 0; i < n; ++i) {
        const std::string existingType = spec.transformJoints[i].type;
        spec.transformJoints[i] = dhJointToTransform (spec.dhJoints[i], existingType);
    }
}
