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

    std::set< std::string > availableSceneRefs;
    availableSceneRefs.insert ("WORLD");
    availableSceneRefs.insert ("RobotBase");
    for (const FrameSpec& frame : spec.sceneFrames)
        availableSceneRefs.insert (frame.name);
    for (const FrameSpec& frame : spec.sceneFrames) {
        if (isEmpty (frame.refFrame))
            errors << QString ("Scene frame %1 requires a refframe.")
                          .arg (QString::fromStdString (frame.name));
        else if (availableSceneRefs.find (frame.refFrame) == availableSceneRefs.end ())
            errors << QString ("Scene frame %1 references unknown refframe %2.")
                          .arg (QString::fromStdString (frame.name),
                                QString::fromStdString (frame.refFrame));
        if (frame.name == frame.refFrame)
            errors << QString ("Scene frame %1 must not reference itself.")
                          .arg (QString::fromStdString (frame.name));
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

    return errors.isEmpty ();
}

// =============================================================================
//  canExportDhJoints
//  说明: 隐藏高级 <DHJoint> 导出的"闸门"检查。三条缺一不可:
//          1) 至少有一行 SE(3) 关节(必须有真值才能投影);
//          2) SE(3) 行数与 DH 投影视图行数一致(防御性,UI 始终保持同步);
//          3) 每一行的 type 必须是 Revolute(简化 DH 不支持其它类型);
//          4) 每一行的 SE(3) -> DH 投影必须无损(pitch=0 且 roll 与
//             pos.xy 方向一致,见 transformJointToDh)。
//        errors 非空时,每条失败原因都会写入。
//        这是 spec.exportDhJointsAdvanced=true 的唯一放行条件。
// =============================================================================
bool RobotModelXmlWriter::canExportDhJoints (const RobotModelSpec& spec, QStringList* errors)
{
    QStringList localErrors;
    QStringList& out = errors == nullptr ? localErrors : *errors;

    if (spec.transformJoints.empty ()) {
        out << "Advanced DH export requires at least one SE(3) joint row.";
        return false;
    }
    if (spec.transformJoints.size () != spec.dhJoints.size ()) {
        out << "Advanced DH export requires DH projection rows to match SE(3) rows.";
        return false;
    }

    bool ok = true;
    for (size_t i = 0; i < spec.transformJoints.size (); ++i) {
        const JointTransformSpec& joint = spec.transformJoints[i];
        if (!isRevoluteType (joint.type)) {
            out << QString ("Advanced DH export only supports Revolute rows; row %1 (%2) is %3.")
                       .arg (static_cast< int > (i + 1))
                       .arg (QString::fromStdString (joint.name))
                       .arg (QString::fromStdString (joint.type));
            ok = false;
            continue;
        }

        bool lossy = false;
        transformJointToDh (joint, &lossy);
        if (lossy) {
            out << QString ("Advanced DH export requires lossless SE(3)->DH projection; row %1 (%2) is lossy.")
                       .arg (static_cast< int > (i + 1))
                       .arg (QString::fromStdString (joint.name));
            ok = false;
        }
    }

    return ok;
}

// =============================================================================
//  makeSerialDeviceXml
//  说明: 把 spec 序列化为 RobWork <SerialDevice>...</SerialDevice> 文本。
//        包含 Base/TCP 帧、关节(DH 或 RPY+Pos)、可选 Drawable、限位、位姿。
// =============================================================================
#if 0
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

    // 双闸门:既要用户主动勾选 exportDhJointsAdvanced,又要所有 SE(3) 行
    // 都能无损投影为 DH。任何一个条件不满足都 fallback 到默认 <Joint> 输出。
    // validate() 在 exportDhJointsAdvanced=true 时会主动调用
    // canExportDhJoints 并把错误塞到 errors,所以正常流程下这里不会再
    // 看到"勾选了但不通过"的情况;但保留 fallback 作为防御性编程。
    // Milestone 1:FixedFrame / ToolFrame 永远是 <Frame> 不参与 DH 表;
    // DHJoint 高级导出仅在转换无损且无 RigidFrame 时启用。
    const bool writeDhJoints = spec.exportDhJointsAdvanced && canExportDhJoints (spec);
    if (writeDhJoints) {
        // writeDhJoints=true 时 canExportDhJoints 已确保 spec 全部 Revolute + 无损
        for (const JointTransformSpec& transformJoint : spec.transformJoints) {
            const DHJointSpec joint = transformJointToDh (transformJoint);
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
        // 默认 SE(3) <Joint> + RigidFrame <Frame> 模式
        for (size_t i = 0; i < spec.transformJoints.size (); ++i) {
            const JointTransformSpec& joint = spec.transformJoints[i];
            if (isFixedFrameType (joint.type) || isToolFrameType (joint.type)) {
                // RigidFrame:输出 <Frame refframe="...">,而非 <Joint>;
                // refframe 取前一个 transformJoint 名(若 i==0 则为 Base)。
                const QString refframe = i > 0
                const QString refframe = i > 0
                    ? QString::fromStdString (spec.transformJoints[i - 1].name)
                    : "Base";
                out << "  <Frame name=\"" << QString::fromStdString (joint.name)
                    << "\" refframe=\"" << refframe << "\">\n";
                out << "    <RPY>" << vector3 (joint.rpyDeg) << "</RPY>\n";
                out << "    <Pos>" << vector3 (joint.pos) << "</Pos>\n";
                if (spec.showFrameAxes)
                    out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
                out << "  </Frame>\n";
            }
            else {
                out << "  <Joint name=\"" << QString::fromStdString (joint.name) << "\" type=\""
                    << QString::fromStdString (joint.type) << "\">\n";
                out << "    <RPY>" << vector3 (joint.rpyDeg) << "</RPY>\n";
                out << "    <Pos>" << vector3 (joint.pos) << "</Pos>\n";
                if (spec.showFrameAxes)
                    out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
                out << "  </Joint>\n";
            }
        }
    }

    // TCP 帧:优先挂到最后一个"可动关节"上(Revolute / Prismatic);
    // 如果没有任何可动关节,则挂在 Base 上。
    {
    {
        QString tcpRef       = "Base";
        const std::vector< size_t > movable = movableJointIndices (spec);
        if (!movable.empty ())
            tcpRef = QString::fromStdString (spec.transformJoints[movable.back ()].name);
        out << "  <Frame name=\"TCP\" refframe=\"" << tcpRef << "\">\n";
        out << "    <RPY>0 0 0</RPY>\n";
        out << "    <Pos>0 0 0</Pos>\n";
        if (spec.showFrameAxes)
            out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
        out << "  </Frame>\n";
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

    // 关节限位(三种类型分开输出,RobWork 要求独立节点)
    // 单位规则:Revolute 是度→rad;Prismatic 米保持。
    for (const JointLimitSpec& limit : spec.limits) {
    for (const JointLimitSpec& limit : spec.limits) {
        out << "  <PosLimit refjoint=\"" << QString::fromStdString (limit.jointName)
            << "\" min=\"" << number (limit.posMin) << "\" max=\""
            << number (limit.posMax) << "\" />\n";
    }
    for (const JointLimitSpec& limit : spec.limits) {
        out << "  <VelLimit refjoint=\"" << QString::fromStdString (limit.jointName)
            << "\" max=\"" << number (limit.velMax) << "\" />\n";
    }
    for (const JointLimitSpec& limit : spec.limits) {
        out << "  <AccLimit refjoint=\"" << QString::fromStdString (limit.jointName)
            << "\" max=\"" << number (limit.accMax) << "\" />\n";
    }

    // 预设位姿:按可动关节顺序取 q;Revolute 度→rad,Prismatic 米保持。
    const std::vector< size_t > movable = movableJointIndices (spec);
    const std::vector< size_t > movable = movableJointIndices (spec);
    for (const PoseSpec& pose : spec.poses) {
        out << "  <Q name=\"" << QString::fromStdString (pose.name) << "\">";
        for (size_t k = 0; k < movable.size (); ++k) {
            if (k > 0)
                out << " ";
            const JointTransformSpec& j = spec.transformJoints[movable[k]];
            const double raw            = pose.q[k];    // validate 已保证 size 一致
            const double value          = isRevoluteType (j.type) ? degToRad (raw) : raw;
            const double value          = isRevoluteType (j.type) ? degToRad (raw) : raw;
            out << number (value);
        }
        out << "</Q>\n";
    }

    out << "</SerialDevice>\n";
    return xml;
}

#endif

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

    const bool writeDhJoints = spec.exportDhJointsAdvanced && canExportDhJoints (spec);
    if (writeDhJoints) {
        for (const JointTransformSpec& transformJoint : spec.transformJoints) {
            const DHJointSpec joint = transformJointToDh (transformJoint);
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
        for (size_t i = 0; i < spec.transformJoints.size (); ++i) {
            const JointTransformSpec& joint = spec.transformJoints[i];
            if (isFixedFrameType (joint.type) || isToolFrameType (joint.type)) {
                const QString refframe = i > 0
                    ? QString::fromStdString (spec.transformJoints[i - 1].name)
                    : "Base";
                out << "  <Frame name=\"" << QString::fromStdString (joint.name)
                    << "\" refframe=\"" << refframe << "\">\n";
                out << "    <RPY>" << vector3 (joint.rpyDeg) << "</RPY>\n";
                out << "    <Pos>" << vector3 (joint.pos) << "</Pos>\n";
                if (spec.showFrameAxes)
                    out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
                out << "  </Frame>\n";
            }
            else {
                out << "  <Joint name=\"" << QString::fromStdString (joint.name) << "\" type=\""
                    << QString::fromStdString (joint.type) << "\">\n";
                out << "    <RPY>" << vector3 (joint.rpyDeg) << "</RPY>\n";
                out << "    <Pos>" << vector3 (joint.pos) << "</Pos>\n";
                if (spec.showFrameAxes)
                    out << "    <Property name=\"ShowFrameAxis\">true</Property>\n";
                out << "  </Joint>\n";
            }
        }
    }

    QString tcpRef = "Base";
    const std::vector< size_t > movable = movableJointIndices (spec);
    if (!movable.empty ())
        tcpRef = QString::fromStdString (spec.transformJoints[movable.back ()].name);
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
            << "\" min=\"" << number (limit.posMin) << "\" max=\""
            << number (limit.posMax) << "\" />\n";
    }
    for (const JointLimitSpec& limit : spec.limits) {
        out << "  <VelLimit refjoint=\"" << QString::fromStdString (limit.jointName)
            << "\" max=\"" << number (limit.velMax) << "\" />\n";
    }
    for (const JointLimitSpec& limit : spec.limits) {
        out << "  <AccLimit refjoint=\"" << QString::fromStdString (limit.jointName)
            << "\" max=\"" << number (limit.accMax) << "\" />\n";
    }

    for (const PoseSpec& pose : spec.poses) {
        out << "  <Q name=\"" << QString::fromStdString (pose.name) << "\">";
        for (size_t k = 0; k < movable.size (); ++k) {
            if (k > 0)
                out << " ";
            const JointTransformSpec& joint = spec.transformJoints[movable[k]];
            const double raw = pose.q[k];
            out << number (isRevoluteType (joint.type) ? degToRad (raw) : raw);
        }
        out << "</Q>\n";
    }

    out << "</SerialDevice>\n";
    return xml;
}

// =============================================================================
//  makeSceneXml
//  说明: 生成场景容器 WorkCell:
//        - RobotBase 帧(Milestone 3 起,用 spec.robotBaseFrame 配置;
//          兜底是 refframe="WORLD" + 0 0 0 位姿);
//        - 依次输出所有 sceneFrames(Table / Workpiece / CameraFrame / MovableBox);
//        - <Include> 引用真正的机器人 .wc.xml(SerialDevice)。
// =============================================================================
QString RobotModelXmlWriter::makeSceneXml (const RobotModelSpec& spec)
{
    const QString robotName = exportedRobotName (spec);
    QString xml;
    QTextStream out (&xml);
    out << "<WorkCell name=\"" << robotName << "Scene\">\n";

    FrameSpec robotBase = spec.robotBaseFrame;
    if (robotBase.name.empty ())
        robotBase.name = "RobotBase";
    if (robotBase.refFrame.empty ())
        robotBase.refFrame = "WORLD";
    robotBase.frameType = SceneFrameType::Fixed;
    writeFrameXml (out, robotBase, spec.showFrameAxes);
    out << "\n";

    for (const FrameSpec& frame : spec.sceneFrames)
        writeFrameXml (out, frame, spec.showFrameAxes);
    if (!spec.sceneFrames.empty ())
        out << "\n";

    out << "  <Include file=\"" << robotName << ".wc.xml\" />\n";
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
        out << "      <MaterialID>" << QString::fromStdString (link.material) << "</MaterialID>\n";
        out << "    </Link>\n";
    }

    out << "  </RigidDevice>\n";
    out << "</DynamicWorkCell>\n";
    return xml;
}

// =============================================================================
//  三个文件路径辅助函数:saveDirectory / robotName + 后缀
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

// =============================================================================
//  saveFiles
//  说明: 先 validate,再按 spec.generateScene / spec.dynamics.generateDynamicWorkCell
//        决定写哪些文件;任何一步失败都会把错误追加到 errors 并返回 false。
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
    out << "  <Frame name=\"" << QString::fromStdString (frame.name)
        << "\" refframe=\"" << QString::fromStdString (frame.refFrame) << "\""
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

DHJointSpec RobotModelXmlWriter::transformJointToDh (const JointTransformSpec& joint, bool* lossy)
{
    DHJointSpec dh;
    dh.name      = joint.name;
    dh.a         = std::sqrt (joint.pos[0] * joint.pos[0] +
                               joint.pos[1] * joint.pos[1]);
    dh.offsetDeg = dh.a > 1e-12 ? std::atan2 (joint.pos[1], joint.pos[0]) * 180.0 / Pi
                                : joint.rpyDeg[0];
    dh.d         = joint.pos[2];
    dh.alphaDeg  = joint.rpyDeg[2];
    if (lossy != nullptr) {
        const bool typeLoss =
            isFixedFrameType (joint.type) || isToolFrameType (joint.type);
        const bool pitchLoss = std::abs (joint.rpyDeg[1]) > 1e-9;
        const bool rollPositionMismatch = dh.a > 1e-12 &&
            normalizedAngleDiffDeg (joint.rpyDeg[0], dh.offsetDeg) > 1e-6;
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
