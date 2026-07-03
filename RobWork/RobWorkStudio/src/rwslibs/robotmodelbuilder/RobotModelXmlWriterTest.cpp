// =============================================================================
//  文件: RobotModelXmlWriterTest.cpp
//  说明: RobotModelXmlWriter 的命令行测试可执行文件(无 GUI 依赖)。
//        不依赖 Google Test,仅用一组断言式的 fail() 返回错误码:
//          - 退出码 0:全部通过;
//          - 退出码 1:任意一条 fail() 触发。
//        覆盖内容:
//          1) 默认数据能 validate;
//          2) SerialDevice XML 包含必要节点(RPY 转换、限位、位姿、Drawable);
//          3) 默认始终以 SE(3) Joint+RPY+Pos 为真值导出;
//          4) 隐藏高级选项仅在全部 Revolute 且无损 DH 投影时导出 <DHJoint>;
//          4) DWC XML 包含 ForceLimit/Link/Mass/COG/Inertia;
//          5) computeLinkPose 在 RPY+Pos 与 DH 两种模式下都给出正确结果;
//          6) 非法输入(零质量/零力限/含空格的名字/重复关节名)被 validate 拦截;
//          7) 把生成的 XML 落到 temp 目录,供人工检查。
// =============================================================================
#include "RobotModelXmlWriter.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace rws;

namespace {
// -----------------------------------------------------------------------------
//  测试辅助:打印错误并返回 1
// -----------------------------------------------------------------------------
int fail (const QString& message)
{
    std::cerr << message.toStdString () << std::endl;
    return 1;
}

/// 文本是否包含子串(简化 QString::contains 调用)
bool contains (const QString& text, const QString& needle)
{
    return text.contains (needle);
}

std::array< double, 3 > rpyRotatedZ (const std::array< double, 3 >& rpyDeg)
{
    const double a = rpyDeg[0] * RobotModelXmlWriter::kPi / 180.0;    // RobWork RPY: Z
    const double b = rpyDeg[1] * RobotModelXmlWriter::kPi / 180.0;    // Y
    const double c = rpyDeg[2] * RobotModelXmlWriter::kPi / 180.0;    // X
    const double ca = std::cos (a);
    const double sa = std::sin (a);
    const double cb = std::cos (b);
    const double sb = std::sin (b);
    const double cc = std::cos (c);
    const double sc = std::sin (c);
    return {{ca * sb * cc + sa * sc,
             sa * sb * cc - ca * sc,
             cb * cc}};
}

bool nearlyEqual (double lhs, double rhs, double eps = 1e-6)
{
    return std::abs (lhs - rhs) <= eps;
}
}    // namespace

// =============================================================================
//  main
//  说明: 测试入口。所有断言失败都用 fail() 立即返回 1;全部通过则返回 0。
// =============================================================================
int main (int argc, char** argv)
{
    QCoreApplication app (argc, argv);

    // ---- 默认模型基础校验 ----
    RobotModelSpec spec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());

    QStringList errors;
    if (!RobotModelXmlWriter::validate (spec, errors))
        return fail ("Default model did not validate: " + errors.join ("; "));

    const QString serialXml = RobotModelXmlWriter::makeSerialDeviceXml (spec);
    const QString sceneXml  = RobotModelXmlWriter::makeSceneXml (spec);

    // SerialDevice XML 必须包含必要的根节点、Base/TCP、6 个 Joint
    if (!contains (serialXml, "<SerialDevice name=\"GenericSixAxis\">"))
        return fail ("Serial XML missing SerialDevice root.");
    if (!contains (serialXml, "<Frame name=\"Base\">"))
        return fail ("Serial XML missing Base frame.");
    if (!contains (serialXml, "<Frame name=\"TCP\""))
        return fail ("Serial XML missing TCP frame.");
    if (serialXml.count ("<Joint name=\"") != 6)
        return fail ("Serial XML must contain six explicit Joint elements.");
    // 默认 RPY 应当把 DH 的 alpha 翻成 RobWork 的 Z-Y-X 顺序
    if (!contains (serialXml,
                   "<Joint name=\"Joint2\" type=\"Revolute\">\n"
                   "    <RPY>0 0 90</RPY>\n"
                   "    <Pos>0.12 0 0</Pos>"))
        return fail ("Joint RPY+Pos defaults should convert DH alpha to RobWork RPY Z-Y-X order.");
    // 默认 Drawable 不应该再包含坐标轴几何(老版本遗留)
    if (contains (serialXml, "<Drawable name=\"Joint1Axis\""))
        return fail ("Default drawables should not include coordinate-axis geometry.");
    // 默认应包含关节外壳
    if (!contains (serialXml, "<Drawable name=\"Joint1Housing\""))
        return fail ("Serial XML missing default joint housing drawable.");
    // 默认应包含关节限位
    if (!contains (serialXml, "<PosLimit refjoint=\"Joint1\""))
        return fail ("Serial XML missing position limit.");
    // 默认不应包含 "Home"(RobWork 会把 Home 当作初始 state,容易引发混淆)
    if (contains (serialXml, "<Q name=\"Home\">"))
        return fail ("Default model should not use Home because RobWork loads it as the initial state.");
    // Ready 位姿应当转为弧度:0, -pi/2, pi/2, 0, 0, 0
    if (!contains (serialXml, "<Q name=\"Ready\">0 -1.5707963267949 1.5707963267949 0 0 0</Q>"))
        return fail ("Ready pose was not converted to radians.");
    // Scene XML 应通过 <Include> 引用机器人文件
    if (!contains (sceneXml, "<Include file=\"GenericSixAxis.wc.xml\" />"))
        return fail ("Scene XML missing include.");

    // ---- DH 表不再作为真值:切换旧 DH mode 也仍然输出 SE(3) Joint XML ----
    RobotModelSpec dhViewSpec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    dhViewSpec.mode           = KinematicsViewMode::DHProjection;
    const QString dhViewXml   = RobotModelXmlWriter::makeSerialDeviceXml (dhViewSpec);
    if (dhViewXml.count ("<DHJoint name=\"") != 0)
        return fail ("DH view mode should not export DHJoint XML by default.");
    if (dhViewXml.count ("<Joint name=\"") != 6)
        return fail ("DH view mode should still export six SE(3) Joint elements.");

    // ---- 隐藏高级选项:只有全部 Revolute 且 SE(3) 可无损投影为 DH 时才允许 <DHJoint> ----
    RobotModelSpec dhSpec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    dhSpec.exportDhJointsAdvanced = true;
    if (!RobotModelXmlWriter::canExportDhJoints (dhSpec, &errors))
        return fail ("Default SE(3) model should be lossless DH exportable: " + errors.join ("; "));
    const QString dhXml = RobotModelXmlWriter::makeSerialDeviceXml (dhSpec);
    if (dhXml.count ("<DHJoint name=\"") != 6)
        return fail ("Advanced DH export must contain six DHJoint elements.");
    if (dhXml.count ("<Joint name=\"") != 0)
        return fail ("Advanced DH export should not also contain explicit Joint elements.");
    if (!contains (dhXml,
                   "<DHJoint name=\"Joint1\" alpha=\"0\" a=\"0\" d=\"0.35\" offset=\"0\" "
                   "type=\"schilling\">\n"
                   "    <Property name=\"ShowFrameAxis\">true</Property>\n"
                   "  </DHJoint>"))
        return fail ("Advanced DH export should emit ShowFrameAxis inside DHJoint elements.");

    RobotModelSpec lossyDh = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    lossyDh.exportDhJointsAdvanced = true;
    lossyDh.transformJoints[0].rpyDeg[1] = 10;
    if (RobotModelXmlWriter::canExportDhJoints (lossyDh, &errors))
        return fail ("Advanced DH export should reject lossy SE(3) rows.");
    if (RobotModelXmlWriter::validate (lossyDh, errors))
        return fail ("validate should reject lossy advanced DH export.");

    RobotModelSpec prismaticDh = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    prismaticDh.exportDhJointsAdvanced = true;
    prismaticDh.transformJoints[1].type = "Prismatic";
    if (RobotModelXmlWriter::canExportDhJoints (prismaticDh, &errors))
        return fail ("Advanced DH export should reject non-Revolute rows.");
    const QString prismaticFallbackXml = RobotModelXmlWriter::makeSerialDeviceXml (prismaticDh);
    if (prismaticFallbackXml.count ("<DHJoint name=\"") != 0)
        return fail ("Invalid advanced DH export should fall back instead of emitting DHJoint XML.");
    if (!contains (prismaticFallbackXml, "<Joint name=\"Joint2\" type=\"Prismatic\">"))
        return fail ("Invalid advanced DH export fallback should preserve SE(3) Joint XML.");

    // ---- 高级 DH 导出:非 Revolute 包括 FixedFrame / ToolFrame 都应拒绝 ----
    {
        RobotModelSpec fixedDh = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        fixedDh.exportDhJointsAdvanced = true;
        fixedDh.transformJoints[0].type = "FixedFrame";
        if (RobotModelXmlWriter::canExportDhJoints (fixedDh, &errors))
            return fail ("Advanced DH export should reject FixedFrame rows.");
    }
    {
        RobotModelSpec toolDh = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        toolDh.exportDhJointsAdvanced = true;
        toolDh.transformJoints[2].type = "ToolFrame";
        if (RobotModelXmlWriter::canExportDhJoints (toolDh, &errors))
            return fail ("Advanced DH export should reject ToolFrame rows.");
    }

    // ---- 高级 DH 导出:roll 与 pos.xy 方向不一致应拒绝 ----
    {
        RobotModelSpec rollMismatch = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        rollMismatch.exportDhJointsAdvanced = true;
        // 默认 Joint2: pos=(0.12, 0, 0),roll=0 → atan2(0, 0.12)=0,一致
        // 把 roll 改成 30 但 pos 仍指向 +X,触发 rollPositionMismatch
        rollMismatch.transformJoints[1].rpyDeg[0] = 30;
        if (RobotModelXmlWriter::canExportDhJoints (rollMismatch, &errors))
            return fail ("Advanced DH export should reject roll vs pos.xy mismatch.");
    }

    // ---- 高级 DH 导出:transformJoints 与 dhJoints 行数不一致应拒绝 ----
    {
        RobotModelSpec lenMismatch = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        lenMismatch.exportDhJointsAdvanced = true;
        // 多塞一个 dhJoints(模拟数据腐烂)
        DHJointSpec extra;
        extra.name      = "JointExtra";
        extra.alphaDeg  = 0;
        extra.a         = 0;
        extra.d         = 0;
        extra.offsetDeg = 0;
        lenMismatch.dhJoints.push_back (extra);
        if (RobotModelXmlWriter::canExportDhJoints (lenMismatch, &errors))
            return fail ("Advanced DH export should reject SE(3)/DH row count mismatch.");
    }

    // ---- 高级 DH 导出:lossy 时 makeSerialDeviceXml 应 fallback 到 <Joint> ----
    {
        RobotModelSpec lossyFallback = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        lossyFallback.exportDhJointsAdvanced = true;
        lossyFallback.transformJoints[0].rpyDeg[1] = 10;   // 触发 lossy
        // validate 应该拒绝(测试 validate 路径)
        if (RobotModelXmlWriter::validate (lossyFallback, errors))
            return fail ("validate should reject lossy advanced DH export.");
        // 直接调 makeSerialDeviceXml(防御性 fallback)应该输出 0 个 DHJoint
        const QString fallbackXml = RobotModelXmlWriter::makeSerialDeviceXml (lossyFallback);
        if (fallbackXml.count ("<DHJoint name=\"") != 0)
            return fail ("Lossy advanced DH export should fall back to default <Joint> XML.");
        if (fallbackXml.count ("<Joint name=\"") != 6)
            return fail ("Lossy advanced DH export fallback should still emit 6 SE(3) Joint elements.");
    }

    // ---- 真值优先:spec.mode = DHProjection 仍以 transformJoints 计算连杆 ----
    // (覆盖编辑 dhJoints 不会反向影响真值 + 连杆几何)
    {
        RobotModelSpec viewOnly = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        viewOnly.mode = KinematicsViewMode::DHProjection;
        // 用户在 DH 投影视图上修改 Joint1 的几何,SE(3) 真值不动
        viewOnly.dhJoints[1].alphaDeg  = 0;
        viewOnly.dhJoints[1].a         = 0.4;
        viewOnly.dhJoints[1].d         = 0.2;
        viewOnly.dhJoints[1].offsetDeg = 90;
        RobotModelXmlWriter::applyLinkGeometry (viewOnly);
        bool foundViewOnly = false;
        for (const DrawableSpec& d : viewOnly.drawables) {
            if (QString::fromStdString (d.name) == "Link1To2") {
                foundViewOnly = true;
                // SE(3) Joint2 的 pos = (0.12, 0, 0),所以 Link1To2 应是 (0.06, 0, 0)
                if (std::abs (d.length - 0.12) > 1e-6 ||
                    std::abs (d.pos[0] - 0.06) > 1e-6 ||
                    std::abs (d.pos[1]) > 1e-6 ||
                    std::abs (d.pos[2]) > 1e-6)
                    return fail ("DH view-only edit should not affect link geometry.");
                break;
            }
        }
        if (!foundViewOnly)
            return fail ("Could not find Link1To2 in DH view-only test.");
    }

    // ---- DWC 模式验证 ----
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

    // ---- 斜向关节:计算 Link1To2 长度与位置 ----
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
            // 不应该再被 X/Z 轴硬编码 RPY 顶住(必须真的旋转到斜向)
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

    // ---- Y 方向有分量的斜向关节:Drawable RPY 必须让圆柱局部 +Z 对齐 Pos 向量 ----
    RobotModelSpec ySkewed = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    ySkewed.transformJoints[1].pos = {{0.12, 0.5, 0.12}};
    RobotModelXmlWriter::applyLinkGeometry (ySkewed);
    bool foundYLink = false;
    for (const DrawableSpec& d : ySkewed.drawables) {
        if (QString::fromStdString (d.name) == "Link1To2") {
            foundYLink = true;
            const double expectedLength = std::sqrt (0.12 * 0.12 + 0.5 * 0.5 + 0.12 * 0.12);
            if (!nearlyEqual (d.length, expectedLength))
                return fail ("Link1To2 length should include Y component.");
            if (!nearlyEqual (d.pos[0], 0.06) || !nearlyEqual (d.pos[1], 0.25) ||
                !nearlyEqual (d.pos[2], 0.06))
                return fail ("Link1To2 center should be half of 0.12 0.5 0.12.");
            const std::array< double, 3 > z = rpyRotatedZ (d.rpyDeg);
            if (!nearlyEqual (z[0] * d.length, 0.12) ||
                !nearlyEqual (z[1] * d.length, 0.5) ||
                !nearlyEqual (z[2] * d.length, 0.12))
                return fail ("Link1To2 RPY should align cylinder +Z with 0.12 0.5 0.12.");
            break;
        }
    }
    if (!foundYLink)
        return fail ("Could not find Link1To2 drawable for Y-skewed case.");

    // ---- DH view mode 仍以 SE(3) 真值计算连杆几何 ----
    RobotModelSpec standardDh = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    standardDh.mode           = KinematicsViewMode::DHProjection;
    RobotModelXmlWriter::applyLinkGeometry (standardDh);
    bool foundDhLink = false;
    for (const DrawableSpec& d : standardDh.drawables) {
        if (QString::fromStdString (d.name) == "Link4To5") {
            foundDhLink = true;
            if (std::abs (d.length - 0.38) > 1e-6)
                return fail (QString ("Link4To5 length should be 0.38 from SE(3) truth, got %1")
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

    // ---- DH 投影视图不会反向改写 SE(3) 真值;连杆几何跟随 transformJoints ----
    RobotModelSpec offsetDh = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    offsetDh.mode           = KinematicsViewMode::DHProjection;
    offsetDh.dhJoints[1].alphaDeg  = 0;
    offsetDh.dhJoints[1].a         = 0.4;
    offsetDh.dhJoints[1].d         = 0.2;
    offsetDh.dhJoints[1].offsetDeg = 90;
    RobotModelXmlWriter::applyLinkGeometry (offsetDh);
    bool foundOffsetLink = false;
    for (const DrawableSpec& d : offsetDh.drawables) {
        if (QString::fromStdString (d.name) == "Link1To2") {
            foundOffsetLink = true;
            if (std::abs (d.length - 0.12) > 1e-6 ||
                std::abs (d.pos[0] - 0.06) > 1e-6 ||
                std::abs (d.pos[1]) > 1e-6 ||
                std::abs (d.pos[2]) > 1e-6)
                return fail ("Link1To2 geometry should follow SE(3) truth, not edited DH projection.");
            break;
        }
    }
    if (!foundOffsetLink)
        return fail ("Could not find Link1To2 drawable for DH projection edit case.");

    // ---- 自动 Drawable 应跟随关节几何重算,而不是用用户改的值 ----
    RobotModelSpec autoDrawable = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    autoDrawable.drawables[6].length = 0.33;
    autoDrawable.drawables[6].rpyDeg = {{10, 20, 30}};
    autoDrawable.drawables[6].pos    = {{0.11, 0.22, 0.33}};
    RobotModelXmlWriter::applyLinkGeometry (autoDrawable);
    if (std::abs (autoDrawable.drawables[6].length - 0.12) > 1e-6 ||
        std::abs (autoDrawable.drawables[6].rpyDeg[1] - 90) > 1e-6 ||
        std::abs (autoDrawable.drawables[6].pos[0] - 0.06) > 1e-6)
        return fail ("Auto Link1To2 drawable geometry should be derived from kinematics.");

    // ---- 非法输入应被 validate 拦截 ----
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
    badName.robotName = "My Robot";      // 含空格,需要被清洗
    if (RobotModelXmlWriter::validate (badName, errors))
        return fail ("Robot names requiring sanitization should fail validation.");

    RobotModelSpec noDrawables = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    noDrawables.generateDrawables = false;
    noDrawables.drawables[0].radius = 0;  // 即使几何非法,未启用 Drawable 时也应通过
    if (!RobotModelXmlWriter::validate (noDrawables, errors))
        return fail ("Disabled drawables should not be validated: " + errors.join ("; "));

    // 重复关节名应被拒绝
    spec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    spec.transformJoints[1].name = spec.transformJoints[0].name;
    if (RobotModelXmlWriter::validate (spec, errors))
        return fail ("Duplicate joint name should fail validation.");

    // 启用 Drawable 后,几何非法应被拒绝
    spec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    spec.drawables[0].radius = 0;
    if (RobotModelXmlWriter::validate (spec, errors))
        return fail ("Zero drawable radius should fail validation.");

    // =====================================================================
    //  DH <-> Joint+RPY+Pos 双向转换(支持 UI 跨表联动)
    // =====================================================================

    // ---- dhJointToTransform 默认 type + 新约定下的位姿 ----
    {
        DHJointSpec dh;
        dh.name      = "Joint1";
        dh.alphaDeg  = 90;
        dh.a         = 0.5;
        dh.d         = 0.3;
        dh.offsetDeg = 45;
        const JointTransformSpec j = RobotModelXmlWriter::dhJointToTransform (dh);
        if (j.name != "Joint1")
            return fail ("dhJointToTransform: name should be preserved.");
        if (j.type != "Revolute")
            return fail ("dhJointToTransform: default type should be Revolute.");
        if (std::abs (j.rpyDeg[0] - 45) > 1e-9)
            return fail ("dhJointToTransform: roll should equal offsetDeg.");
        if (std::abs (j.rpyDeg[1]) > 1e-9)
            return fail ("dhJointToTransform: pitch should be 0.");
        if (std::abs (j.rpyDeg[2] - 90) > 1e-9)
            return fail ("dhJointToTransform: yaw should equal alphaDeg.");
        // 新约定:pos = (a*cos(offset), a*sin(offset), d)
        const double theta = 45.0 * RobotModelXmlWriter::kPi / 180.0;
        const double ex    = 0.5 * std::cos (theta);
        const double ey    = 0.5 * std::sin (theta);
        if (std::abs (j.pos[0] - ex) > 1e-9)
            return fail ("dhJointToTransform: pos[0] should equal a*cos(offset).");
        if (std::abs (j.pos[1] - ey) > 1e-9)
            return fail ("dhJointToTransform: pos[1] should equal a*sin(offset).");
        if (std::abs (j.pos[2] - 0.3) > 1e-9)
            return fail ("dhJointToTransform: pos[2] should equal d.");
    }

    // ---- dhJointToTransform:offset=0 时,pos 应退化为 (a, 0, d) ----
    {
        DHJointSpec dh;
        dh.name      = "Joint1b";
        dh.alphaDeg  = 0;
        dh.a         = 0.5;
        dh.d         = 0.3;
        dh.offsetDeg = 0;
        const JointTransformSpec j = RobotModelXmlWriter::dhJointToTransform (dh);
        if (std::abs (j.pos[0] - 0.5) > 1e-9 ||
            std::abs (j.pos[1]) > 1e-9 ||
            std::abs (j.pos[2] - 0.3) > 1e-9)
            return fail ("dhJointToTransform: offset=0 should degenerate to (a, 0, d).");
    }

    // ---- dhJointToTransform 保留已有 type(例如 Prismatic)----
    {
        DHJointSpec dh;
        dh.name      = "Joint2";
        dh.alphaDeg  = 0;
        dh.a         = 0;
        dh.d         = 0.2;
        dh.offsetDeg = 0;
        const JointTransformSpec j =
            RobotModelXmlWriter::dhJointToTransform (dh, "Prismatic");
        if (j.type != "Prismatic")
            return fail ("dhJointToTransform: existing type should be preserved.");
    }

    // ---- transformJointToDh 无损场景(rpy/pos 内部一致:pos[1]=a*sin(roll),pitch=0)----
    {
        JointTransformSpec in;
        in.name   = "Joint3";
        in.type   = "Revolute";
        in.rpyDeg = {{45, 0, 90}};
        // 与 roll=45, a=0.5 一致:pos[0] = 0.5*cos(45), pos[1] = 0.5*sin(45)
        const double theta = 45.0 * RobotModelXmlWriter::kPi / 180.0;
        in.pos    = {{0.5 * std::cos (theta), 0.5 * std::sin (theta), 0.3}};
        bool lossy = true;    // 故意预置 true,确认函数会改写
        const DHJointSpec dh = RobotModelXmlWriter::transformJointToDh (in, &lossy);
        if (dh.name != "Joint3")
            return fail ("transformJointToDh: name should be preserved.");
        if (lossy)
            return fail ("transformJointToDh: consistent input should be lossless.");
        if (std::abs (dh.alphaDeg - 90) > 1e-9)
            return fail ("transformJointToDh: alpha should equal yaw.");
        if (std::abs (dh.a - 0.5) > 1e-9)
            return fail ("transformJointToDh: a should equal sqrt(px^2+py^2).");
        if (std::abs (dh.d - 0.3) > 1e-9)
            return fail ("transformJointToDh: d should equal pos[2].");
        if (std::abs (dh.offsetDeg - 45) > 1e-9)
            return fail ("transformJointToDh: offset should equal atan2(py, px).");
    }

    // ---- transformJointToDh 有损场景:pitch != 0 触发 lossy ----
    {
        JointTransformSpec in;
        in.name   = "Joint4";
        in.type   = "Revolute";
        in.rpyDeg = {{45, 30, 90}};   // pitch != 0
        in.pos    = {{0.5, 0.2, 0.3}};
        bool lossy = false;
        const DHJointSpec dh = RobotModelXmlWriter::transformJointToDh (in, &lossy);
        if (!lossy)
            return fail ("transformJointToDh: pitch=30 should set lossy=true.");
        // 位置分量按新约定反推
        const double aExp    = std::sqrt (0.5 * 0.5 + 0.2 * 0.2);
        const double offExp  = std::atan2 (0.2, 0.5) * 180.0 / RobotModelXmlWriter::kPi;
        if (std::abs (dh.a - aExp) > 1e-9)
            return fail ("transformJointToDh: a should follow pos (new convention).");
        if (std::abs (dh.offsetDeg - offExp) > 1e-9)
            return fail ("transformJointToDh: offset should follow pos (new convention).");
        if (std::abs (dh.alphaDeg - 90) > 1e-9 ||
            std::abs (dh.d - 0.3) > 1e-9)
            return fail ("transformJointToDh: alpha/d should not change in lossy case.");
    }

    // ---- transformJointToDh 有损场景:roll 与 Pos 方向不一致触发 lossy ----
    {
        JointTransformSpec in;
        in.name   = "Joint4b";
        in.type   = "Revolute";
        in.rpyDeg = {{30, 0, 90}};      // roll=30, but position points along +X
        in.pos    = {{0.5, 0.0, 0.3}}; // atan2(py, px)=0
        bool lossy = false;
        const DHJointSpec dh = RobotModelXmlWriter::transformJointToDh (in, &lossy);
        if (!lossy)
            return fail ("transformJointToDh: roll/position mismatch should set lossy=true.");
        if (std::abs (dh.offsetDeg) > 1e-9 ||
            std::abs (dh.a - 0.5) > 1e-9 ||
            std::abs (dh.d - 0.3) > 1e-9 ||
            std::abs (dh.alphaDeg - 90) > 1e-9)
            return fail ("transformJointToDh: projection should still be derived from position/yaw.");
    }

    // ---- transformJointToDh:不传 lossy 参数也能用(默认 nullptr 不写)----
    {
        JointTransformSpec in;
        in.rpyDeg = {{10, 0, 20}};
        const double theta = 10.0 * RobotModelXmlWriter::kPi / 180.0;
        in.pos    = {{0.1 * std::cos (theta), 0.1 * std::sin (theta), 0.2}};
        const DHJointSpec dh = RobotModelXmlWriter::transformJointToDh (in);
        if (std::abs (dh.a - 0.1) > 1e-9 ||
            std::abs (dh.offsetDeg - 10) > 1e-9 ||
            std::abs (dh.d - 0.2) > 1e-9 ||
            std::abs (dh.alphaDeg - 20) > 1e-9)
            return fail ("transformJointToDh: should work without lossy out-param.");
    }

    // ---- transformJointToDh:xy 位移为零时保留 roll 作为 offset ----
    {
        JointTransformSpec in;
        in.name   = "Joint4c";
        in.type   = "Revolute";
        in.rpyDeg = {{35, 0, 70}};
        in.pos    = {{0, 0, 0.25}};
        bool lossy = true;
        const DHJointSpec dh = RobotModelXmlWriter::transformJointToDh (in, &lossy);
        if (lossy)
            return fail ("transformJointToDh: zero xy position with pitch=0 should be lossless.");
        if (std::abs (dh.a) > 1e-9 ||
            std::abs (dh.offsetDeg - 35) > 1e-9 ||
            std::abs (dh.d - 0.25) > 1e-9 ||
            std::abs (dh.alphaDeg - 70) > 1e-9)
            return fail ("transformJointToDh: zero xy position should preserve roll as offset.");
    }

    // ---- DH -> Transform -> DH 无损往返 ----
    {
        DHJointSpec orig;
        orig.name      = "Joint5";
        orig.alphaDeg  = -45;
        orig.a         = 0.4;
        orig.d         = 0.2;
        orig.offsetDeg = 30;
        const JointTransformSpec mid = RobotModelXmlWriter::dhJointToTransform (orig);
        const DHJointSpec back       = RobotModelXmlWriter::transformJointToDh (mid);
        if (std::abs (back.alphaDeg - orig.alphaDeg) > 1e-9 ||
            std::abs (back.a - orig.a) > 1e-9 ||
            std::abs (back.d - orig.d) > 1e-9 ||
            std::abs (back.offsetDeg - orig.offsetDeg) > 1e-9 ||
            back.name != orig.name)
            return fail ("DH -> Transform -> DH round-trip should be lossless.");
    }

    // ---- Transform -> DH -> Transform 无损往返(rpy/pos 内部一致)----
    {
        JointTransformSpec origJ;
        origJ.name   = "Joint6";
        origJ.type   = "Revolute";
        origJ.rpyDeg = {{10, 0, 20}};
        // 与 roll=10, a=0.1 一致
        const double theta = 10.0 * RobotModelXmlWriter::kPi / 180.0;
        origJ.pos    = {{0.1 * std::cos (theta), 0.1 * std::sin (theta), 0.2}};
        const DHJointSpec midD = RobotModelXmlWriter::transformJointToDh (origJ);
        const JointTransformSpec backJ =
            RobotModelXmlWriter::dhJointToTransform (midD, origJ.type);
        if (backJ.type != origJ.type)
            return fail ("Transform -> DH -> Transform: type should be preserved.");
        if (std::abs (backJ.rpyDeg[0] - 10) > 1e-9 ||
            std::abs (backJ.rpyDeg[1]) > 1e-9 ||
            std::abs (backJ.rpyDeg[2] - 20) > 1e-9)
            return fail ("Transform -> DH -> Transform: RPY should round-trip.");
        if (std::abs (backJ.pos[0] - origJ.pos[0]) > 1e-9 ||
            std::abs (backJ.pos[1] - origJ.pos[1]) > 1e-9 ||
            std::abs (backJ.pos[2] - origJ.pos[2]) > 1e-9)
            return fail ("Transform -> DH -> Transform: Pos should round-trip.");
    }

    // ---- applyDhInputToTransform:重写 Transform 行,但保留用户的 type ----
    {
        RobotModelSpec syncSpec =
            RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        // 把第 3 行(索引 2)的 type 改成 Prismatic,模拟用户手动编辑
        syncSpec.transformJoints[2].type = "Prismatic";
        // 同时把第 3 行的 DH 值改一下,确保 sync 会更新 Transform
        syncSpec.dhJoints[2].alphaDeg  = 12.34;
        syncSpec.dhJoints[2].offsetDeg = 56.78;
        syncSpec.dhJoints[2].a         = 0.111;
        syncSpec.dhJoints[2].d         = 0.222;
        RobotModelXmlWriter::applyDhInputToTransform (syncSpec);
        if (syncSpec.transformJoints[2].type != "Prismatic")
            return fail ("applyDhInputToTransform: custom type should be preserved.");
        const DHJointSpec& d = syncSpec.dhJoints[2];
        const JointTransformSpec& j = syncSpec.transformJoints[2];
        // RPY = (offset, 0, alpha)
        if (std::abs (j.rpyDeg[0] - d.offsetDeg) > 1e-9 ||
            std::abs (j.rpyDeg[1]) > 1e-9 ||
            std::abs (j.rpyDeg[2] - d.alphaDeg) > 1e-9)
            return fail ("applyDhInputToTransform: RPY should follow DH.");
        // pos = (a*cos(offset), a*sin(offset), d)
        const double theta = d.offsetDeg * RobotModelXmlWriter::kPi / 180.0;
        if (std::abs (j.pos[0] - d.a * std::cos (theta)) > 1e-9 ||
            std::abs (j.pos[1] - d.a * std::sin (theta)) > 1e-9 ||
            std::abs (j.pos[2] - d.d) > 1e-9)
            return fail ("applyDhInputToTransform: pos should follow DH (new convention).");
    }

    // ---- refreshDhProjectionFromTransform:重写 DH 行(使用与 roll 一致的 pos)----
    {
        RobotModelSpec syncSpec =
            RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        // 修改第 2 行(索引 1)的 RPY/Pos,使用一致的数据
        syncSpec.transformJoints[1].rpyDeg = {{10, 0, 20}};
        const double theta = 10.0 * RobotModelXmlWriter::kPi / 180.0;
        syncSpec.transformJoints[1].pos    = {{0.3 * std::cos (theta), 0.3 * std::sin (theta), 0.4}};
        RobotModelXmlWriter::refreshDhProjectionFromTransform (syncSpec);
        if (std::abs (syncSpec.dhJoints[1].offsetDeg - 10) > 1e-9 ||
            std::abs (syncSpec.dhJoints[1].alphaDeg - 20) > 1e-9 ||
            std::abs (syncSpec.dhJoints[1].a - 0.3) > 1e-9 ||
            std::abs (syncSpec.dhJoints[1].d - 0.4) > 1e-9)
            return fail ("refreshDhProjectionFromTransform: values should follow Transform (consistent).");
    }

    // ---- refreshDhProjectionFromTransform:有损分量(pitch)被丢弃 ----
    {
        RobotModelSpec syncSpec =
            RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        // 故意带 pitch=30 触发有损
        syncSpec.transformJoints[0].rpyDeg = {{20, 30, 40}};   // roll=20, pitch=30, yaw=40
        syncSpec.transformJoints[0].pos    = {{0.5, 0.2, 0.6}};
        RobotModelXmlWriter::refreshDhProjectionFromTransform (syncSpec);
        // pos 仍然按新约定反推,所以 a / offset / d 走 pos 来的值
        const double aExp   = std::sqrt (0.5 * 0.5 + 0.2 * 0.2);
        const double offExp = std::atan2 (0.2, 0.5) * 180.0 / RobotModelXmlWriter::kPi;
        if (std::abs (syncSpec.dhJoints[0].offsetDeg - offExp) > 1e-9 ||
            std::abs (syncSpec.dhJoints[0].alphaDeg - 40) > 1e-9 ||
            std::abs (syncSpec.dhJoints[0].a - aExp) > 1e-9 ||
            std::abs (syncSpec.dhJoints[0].d - 0.6) > 1e-9)
            return fail ("refreshDhProjectionFromTransform: pos-derived values should be correct.");
        // 把"pitch 被丢"用 round-trip 验证:dhToTransform 回来的 rpy[1] 应为 0
        const JointTransformSpec backJ =
            RobotModelXmlWriter::dhJointToTransform (syncSpec.dhJoints[0]);
        if (std::abs (backJ.rpyDeg[1]) > 1e-9)
            return fail ("refreshDhProjectionFromTransform: pitch should be discarded.");
    }

    // ---- refresh*/apply* 两侧长度不一致时,不抛异常,也不增删行 ----
    {
        RobotModelSpec syncSpec =
            RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        // 故意把 Transform 加长
        JointTransformSpec extra;
        extra.name   = "JointExtra";
        extra.type   = "Revolute";
        extra.rpyDeg = {{0, 0, 0}};
        extra.pos    = {{0, 0, 0}};
        syncSpec.transformJoints.push_back (extra);
        const size_t dhBefore     = syncSpec.dhJoints.size ();
        const size_t transformBefore = syncSpec.transformJoints.size ();
        RobotModelXmlWriter::applyDhInputToTransform (syncSpec);
        RobotModelXmlWriter::refreshDhProjectionFromTransform (syncSpec);
        if (syncSpec.dhJoints.size () != dhBefore ||
            syncSpec.transformJoints.size () != transformBefore)
            return fail ("refresh*/apply*: should not change vector sizes.");
    }

    // =====================================================================
    //  Milestone 1:关节/帧语义补齐 —— Prismatic + FixedFrame + ToolFrame
    // =====================================================================

    // ---- 5 Revolute + 1 Prismatic:Q 长度为 6,Prismatic 值保持米不转弧度 ----
    {
        RobotModelSpec specRP = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        // 把 Joint3 改成 Prismatic,默认 6 个 pose 都是 0 单位,语义对 Prismatic = 0m
        specRP.transformJoints[2].type = "Prismatic";
        specRP.transformJoints[2].pos  = {{0.3, 0, 0.05}};
        // 改 limit 用米而不是度
        specRP.limits[2].posMin = -1.0;
        specRP.limits[2].posMax = 1.0;
        specRP.limits[2].velMax = 0.5;
        specRP.limits[2].accMax = 1.0;
        QStringList errRP;
        if (!RobotModelXmlWriter::validate (specRP, errRP))
            return fail ("5 Revolute + 1 Prismatic should validate: " + errRP.join ("; "));
        const QString xmlRP = RobotModelXmlWriter::makeSerialDeviceXml (specRP);
        if (!contains (xmlRP, "<Joint name=\"Joint3\" type=\"Prismatic\">"))
            return fail ("Prismatic joint should emit type=\"Prismatic\".");
        // Prismatic limit 应原文输出(m, 不转弧度)
        if (!contains (xmlRP, "<PosLimit refjoint=\"Joint3\" min=\"-1\" max=\"1\" />"))
            return fail ("Prismatic limit should be emitted in meters.");
        if (!contains (xmlRP, "<VelLimit refjoint=\"Joint3\" max=\"0.5\" />"))
            return fail ("Prismatic velocity limit should be emitted in m/s.");
        // Revolute limit(Joint1 = +/- 180 度)在 XML 中应该已经被换算成弧度 (-3.14159)
        if (!contains (xmlRP, "<PosLimit refjoint=\"Joint1\" min=\"-180\" max=\"180\" />"))
            return fail ("Revolute position limit should be emitted in degrees.");
        if (!contains (xmlRP, "<VelLimit refjoint=\"Joint1\" max=\"120\" />"))
            return fail ("Revolute velocity limit should be emitted in deg/s.");
        if (!contains (xmlRP, "<AccLimit refjoint=\"Joint1\" max=\"360\" />"))
            return fail ("Revolute acceleration limit should be emitted in deg/s^2.");
    }

    // ---- FixedFrame 输出为 <Frame>,不进入 Q 维度,不引用 limit ----
    {
        RobotModelSpec fixedSpec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        fixedSpec.transformJoints[1].type = "FixedFrame";
        // 自验:添加一条引用 FixedFrame 的 limit 必须被 validate 拦截
        JointLimitSpec badLimit;
        badLimit.jointName = "Joint2";        // Joint2 被改成 FixedFrame
        badLimit.posMin    = -10;
        badLimit.posMax    = 10;
        badLimit.velMax    = 1;
        badLimit.accMax    = 1;
        // Bad limit 通过 spec.limits[1] 注入(原 Joint2 的 limit)
        fixedSpec.limits[1] = badLimit;
        // 同时让 zreo pose 仍能过;但 limit 会失败
        QStringList errFixed;
        if (RobotModelXmlWriter::validate (fixedSpec, errFixed))
            return fail ("Limit referencing a FixedFrame row should be rejected.");
        // 修正 limit 指向 Joint1(Revolute)
        fixedSpec.limits[1].jointName = "Joint1";
        // 同时把默认 poses 的 q 缩到可动关节数(5)
        for (PoseSpec& p : fixedSpec.poses)
            p.q = std::vector< double > (5, 0.0);
        if (!RobotModelXmlWriter::validate (fixedSpec, errFixed))
            return fail ("FixedFrame model with limits only on movable joints should validate: " +
                         errFixed.join ("; "));
        const QString xmlFixed = RobotModelXmlWriter::makeSerialDeviceXml (fixedSpec);
        if (!contains (xmlFixed, "<Frame name=\"Joint2\" refframe=\"Joint1\">"))
            return fail ("FixedFrame row should emit <Frame refframe=...> with previous frame.");
        if (xmlFixed.contains ("<Joint name=\"Joint2\""))
            return fail ("FixedFrame row should NOT emit <Joint name=\"Joint2\".");
        // Q 维度 = 可动关节数 = 5;现有 pose.q 若长度错,会被 validate 拒
        fixedSpec.poses.clear ();
        PoseSpec p;
        p.name = "Zero";
        p.q    = std::vector< double > (5, 0.0);    // 5 个可动关节
        fixedSpec.poses.push_back (p);
        if (!RobotModelXmlWriter::validate (fixedSpec, errFixed))
            return fail ("FixedFrame pose.q length should match movable joint count.");
        const QString xmlFixedPose = RobotModelXmlWriter::makeSerialDeviceXml (fixedSpec);
        if (xmlFixedPose.count ("<Q") == 0)
            return fail ("At least one <Q> row should be emitted.");
    }

    // ---- ToolFrame 同上,并把 TCP ref 指向最后一个可动关节 ----
    {
        RobotModelSpec toolSpec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        // 把最后一行的 type 改成 ToolFrame
        toolSpec.transformJoints.back ().type = "ToolFrame";
        // 同步:清掉对应的 limit 与 dynamics link(Milestone 1 应该在 UI 里自动维护)
        toolSpec.limits.pop_back ();
        toolSpec.dynamics.forceLimits.pop_back ();
        toolSpec.dynamics.links.pop_back ();
        toolSpec.poses.clear ();
        PoseSpec p;
        p.name = "Zero";
        p.q    = std::vector< double >(5, 0.0);    // 5 可动
        toolSpec.poses.push_back (p);
        QStringList errTool;
        if (!RobotModelXmlWriter::validate (toolSpec, errTool))
            return fail ("ToolFrame model should validate: " + errTool.join ("; "));
        const QString xmlTool = RobotModelXmlWriter::makeSerialDeviceXml (toolSpec);
        if (!contains (xmlTool, "<Frame name=\"Joint6\" refframe=\""))
            return fail ("ToolFrame row should emit as <Frame> in the device XML.");
        // TCP refframe = 最后一个可动关节 = Joint5
        if (!contains (xmlTool, "<Frame name=\"TCP\" refframe=\"Joint5\">"))
            return fail ("TCP should attach to the last movable joint (Joint5).");
        // ForceLimit 引用 ToolFrame 必须被拒
        auto lastIndex = toolSpec.dynamics.forceLimits.size () - 1;
        toolSpec.dynamics.forceLimits[lastIndex].jointName = toolSpec.transformJoints.back ().name;
        toolSpec.dynamics.generateDynamicWorkCell = true;
        if (RobotModelXmlWriter::validate (toolSpec, errTool))
            return fail ("ForceLimit referencing ToolFrame should be rejected.");
    }

    // ---- 没有可动关节(全是 FixedFrame / ToolFrame)时,生成合法 Frame-only XML,
    //      且不应输出任何 PosLimit/VelLimit/AccLimit/<Q> ----
    {
        RobotModelSpec framesOnly = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        for (auto& j : framesOnly.transformJoints)
            j.type = "FixedFrame";
        // limit / forceLimits / poses 都清空,drawables 也清空
        framesOnly.limits.clear ();
        framesOnly.dynamics.forceLimits.clear ();
        framesOnly.poses.clear ();
        framesOnly.drawables.clear ();
        QStringList errFo;
        if (!RobotModelXmlWriter::validate (framesOnly, errFo))
            return fail ("Frame-only spec should validate: " + errFo.join ("; "));
        const QString xmlFo = RobotModelXmlWriter::makeSerialDeviceXml (framesOnly);
        if (xmlFo.contains ("<Q "))
            return fail ("Frame-only model should NOT output any <Q>.");
        if (xmlFo.contains ("<PosLimit") || xmlFo.contains ("<VelLimit") ||
            xmlFo.contains ("<AccLimit"))
            return fail ("Frame-only model should NOT output PosLimit/VelLimit/AccLimit.");
        if (xmlFo.contains ("<Joint "))
            return fail ("Frame-only model should NOT output any <Joint>.");
        // TCP 应该挂到 Base(没有可动关节时的回退策略)
        if (!contains (xmlFo, "<Frame name=\"TCP\" refframe=\"Base\">"))
            return fail ("Frame-only model: TCP should fall back to Base.");
    }

    // ---- validate 拒绝 q-vector 维度错误 ----
    {
        RobotModelSpec badPose = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        badPose.poses.clear ();
        PoseSpec wrong;
        wrong.name = "Wrong";
        wrong.q    = std::vector< double > {0, 0, 0};    // 只 3 个,模型有 6 个可动
        badPose.poses.push_back (wrong);
        QStringList errBp;
        if (RobotModelXmlWriter::validate (badPose, errBp))
            return fail ("pose.q length mismatch should fail validation.");
        if (!errBp.join (" ").contains ("Wrong"))
            return fail ("pose mismatch error should name the offending pose.");
    }

    // ---- validate 拒绝重复的 frame 名(Joint2 重命名为已存在的 Base 也算冲突)----
    {
        RobotModelSpec dup = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        dup.transformJoints[1].name = "Base";    // 冲突:与系统 Base
        QStringList errDup;
        if (RobotModelXmlWriter::validate (dup, errDup))
            return fail ("Joint name colliding with Base should fail validation.");
    }

    // =====================================================================
    //  Milestone 2:可变关节数
    // =====================================================================

    // ---- 3 轴模型:validate + 生成 XML ----
    {
        RobotModelSpec threeAxis = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        // 砍到只剩 3 关节 + 同步 limit/pose/dhJoints
        while (threeAxis.transformJoints.size () > 3)
            threeAxis.transformJoints.pop_back ();
        while (threeAxis.dhJoints.size () > 3)
            threeAxis.dhJoints.pop_back ();
        while (threeAxis.limits.size () > 3)
            threeAxis.limits.pop_back ();
        threeAxis.dynamics.forceLimits.clear ();
        threeAxis.dynamics.links.clear ();
        threeAxis.poses.clear ();
        // 每行 1 可动关节 → pose.q 长度应为 3
        PoseSpec zero;
        zero.name = "Zero";
        zero.q    = {0.0, 0.0, 0.0};
        threeAxis.poses.push_back (zero);
        // 同步 drawables:删 housings / auto link drawables
        threeAxis.drawables.erase (
            std::remove_if (threeAxis.drawables.begin (), threeAxis.drawables.end (),
                            [&] (const DrawableSpec& d) {
                                const QString n = QString::fromStdString (d.name);
                                return n.endsWith ("Housing") ||
                                       QRegularExpression ("^Link\\d+To\\d+$").match (n).hasMatch ();
                            }),
            threeAxis.drawables.end ());
        QStringList err3;
        if (!RobotModelXmlWriter::validate (threeAxis, err3))
            return fail ("3-axis model should validate: " + err3.join ("; "));
        const QString xml3 = RobotModelXmlWriter::makeSerialDeviceXml (threeAxis);
        if (xml3.count ("<Joint name=\"") != 3)
            return fail ("3-axis model should emit exactly 3 <Joint name=...>.");
        if (!contains (xml3, "<Frame name=\"TCP\" refframe=\"Joint3\">"))
            return fail ("3-axis TCP should attach to Joint3.");
    }

    // ---- 7 轴模型:同上 ----
    {
        RobotModelSpec sevenAxis = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        JointTransformSpec extra;
        extra.name   = "Joint7";
        extra.type   = "Revolute";
        extra.rpyDeg = {{0, 0, 90}};
        extra.pos    = {{0, 0, 0.1}};
        sevenAxis.transformJoints.push_back (extra);
        DHJointSpec dh7;
        dh7.name = extra.name;
        sevenAxis.dhJoints.push_back (dh7);
        JointLimitSpec lim7;
        lim7.jointName = extra.name;
        lim7.posMin    = -180;
        lim7.posMax    = 180;
        lim7.velMax    = 120;
        lim7.accMax    = 360;
        sevenAxis.limits.push_back (lim7);
        // poses 长度扩展到 7
        sevenAxis.poses.clear ();
        PoseSpec zero;
        zero.name = "Zero";
        zero.q    = std::vector< double > (7, 0.0);
        sevenAxis.poses.push_back (zero);
        QStringList err7;
        if (!RobotModelXmlWriter::validate (sevenAxis, err7))
            return fail ("7-axis model should validate: " + err7.join ("; "));
        const QString xml7 = RobotModelXmlWriter::makeSerialDeviceXml (sevenAxis);
        if (xml7.count ("<Joint name=\"") != 7)
            return fail ("7-axis model should emit exactly 7 <Joint name=...>.");
        if (!contains (xml7, "<Frame name=\"TCP\" refframe=\"Joint7\">"))
            return fail ("7-axis TCP should attach to Joint7.");
    }

    // ---- movableJointCount 与 fields 同步 ----
    {
        RobotModelSpec spec5 = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        // 5R + 1P:可动关节计数应为 6,但第 3 行是 Prismatic
        spec5.transformJoints[2].type = "Prismatic";
        if (RobotModelXmlWriter::movableJointCount (spec5) != 6)
            return fail ("movableJointCount should return 6 for any Revolute/Prismatic mix.");
    }

    // ---- applyDefaultDrawables rebuilds default geometry from the current joint table ----
    {
        RobotModelSpec drawableSpec =
            RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        const std::string removed = drawableSpec.transformJoints[2].name;
        drawableSpec.transformJoints.erase (drawableSpec.transformJoints.begin () + 2);
        DrawableSpec custom;
        custom.name           = "CameraBody";
        custom.refFrame       = "Joint1";
        custom.shape          = "Cylinder";
        custom.radius         = 0.01;
        custom.length         = 0.02;
        custom.rpyDeg         = {{0, 0, 0}};
        custom.pos            = {{0, 0, 0}};
        custom.rgb            = {{1, 0, 0}};
        custom.collisionModel = false;
        drawableSpec.drawables.push_back (custom);

        RobotModelXmlWriter::applyDefaultDrawables (drawableSpec);

        int housingCount = 0;
        int linkCount    = 0;
        bool keptCustom  = false;
        for (const DrawableSpec& d : drawableSpec.drawables) {
            const QString name = QString::fromStdString (d.name);
            if (name.endsWith ("Housing"))
                ++housingCount;
            if (QRegularExpression ("^Link\\d+To\\d+$").match (name).hasMatch ())
                ++linkCount;
            if (d.name == "CameraBody" && d.refFrame == "Joint1")
                keptCustom = true;
            if (d.refFrame == removed)
                return fail ("applyDefaultDrawables should not leave geometry on a removed joint.");
        }
        if (housingCount != 5)
            return fail ("applyDefaultDrawables should create one housing per remaining row.");
        if (linkCount != 4)
            return fail ("applyDefaultDrawables should create one auto link per adjacent row pair.");
        if (!keptCustom)
            return fail ("applyDefaultDrawables should preserve custom drawables.");
    }

    // ---- computeLinkPose:7 关节 → 应有 6 条 link ----
    {
        RobotModelSpec sevenAxis = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        JointTransformSpec extra;
        extra.name   = "Joint7";
        extra.type   = "Revolute";
        extra.rpyDeg = {{0, 0, 0}};
        extra.pos    = {{0, 0, 0.05}};
        sevenAxis.transformJoints.push_back (extra);
        std::array< double, 3 > pos, rpy;
        double len = 0;
        RobotModelXmlWriter::computeLinkPose (sevenAxis, 5, pos, rpy, len);
        if (std::abs (len - 0.05) > 1e-6)
            return fail ("computeLinkPose on 7-axis: link 6 should be 0.05m.");
    }

    // ---- applyLinkGeometry 同步到 7 轴 ----
    {
        RobotModelSpec sevenAxis = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        // 加 Joint7
        JointTransformSpec extra;
        extra.name   = "Joint7";
        extra.type   = "Revolute";
        extra.rpyDeg = {{0, 0, 0}};
        extra.pos    = {{0, 0, 0.1}};
        sevenAxis.transformJoints.push_back (extra);
        // 追加 Joint7Housing + Link6To7
        DrawableSpec h;
        h.name     = "Joint7Housing";
        h.refFrame = "Joint7";
        h.shape    = "Cylinder";
        h.radius   = 0.04;
        h.length   = 0.06;
        h.rgb      = {{0.45, 0.45, 0.48}};
        sevenAxis.drawables.push_back (h);
        DrawableSpec l;
        l.name             = "Link6To7";
        l.refFrame         = "Joint6";
        l.shape            = "Cylinder";
        l.radius           = 0.04;
        l.length           = 0;
        l.rgb              = {{0.35, 0.45, 0.65}};
        l.autoLinkGeometry = true;
        sevenAxis.drawables.push_back (l);
        RobotModelXmlWriter::applyLinkGeometry (sevenAxis);
        bool found = false;
        for (const DrawableSpec& d : sevenAxis.drawables) {
            if (QString::fromStdString (d.name) == "Link6To7") {
                found = true;
                if (std::abs (d.length - 0.1) > 1e-6)
                    return fail ("Link6To7 length should derive from Joint7.pos.z.");
                break;
            }
        }
        if (!found)
            return fail ("applyLinkGeometry should update Link6To7 length.");
    }

    // =====================================================================
    //  Milestone 3: Frame / WorkCell scene objects
    // =====================================================================
    {
        RobotModelSpec sceneSpec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        sceneSpec.sceneFrames.clear ();    // makeDefaultSixAxisModel 已经预填 4 个,测试覆盖它们
        sceneSpec.robotBaseFrame.pos   = {{0.4, -0.2, 0.75}};
        sceneSpec.robotBaseFrame.rpyDeg = {{0, 0, 90}};

        FrameSpec table;
        table.name     = "Table";
        table.refFrame = "WORLD";
        table.frameType = SceneFrameType::Fixed;
        table.rpyDeg   = {{0, 0, 0}};
        table.pos      = {{0.7, 0, 0.35}};
        sceneSpec.sceneFrames.push_back (table);

        FrameSpec workpiece;
        workpiece.name     = "Workpiece";
        workpiece.refFrame = "Table";
        workpiece.frameType = SceneFrameType::Fixed;
        workpiece.daf      = true;
        workpiece.rpyDeg   = {{0, 0, 0}};
        workpiece.pos      = {{0, 0, 0.08}};
        sceneSpec.sceneFrames.push_back (workpiece);

        FrameSpec camera;
        camera.name     = "CameraFrame";
        camera.refFrame = "RobotBase";
        camera.frameType = SceneFrameType::Normal;
        camera.rpyDeg   = {{180, 0, 0}};
        camera.pos      = {{0.2, 0.1, 1.2}};
        sceneSpec.sceneFrames.push_back (camera);

        FrameSpec movableBox;
        movableBox.name     = "MovableBox";
        movableBox.refFrame = "WORLD";
        movableBox.frameType = SceneFrameType::Movable;
        movableBox.daf      = true;
        movableBox.rpyDeg   = {{0, 0, 0}};
        movableBox.pos      = {{0.1, 0.2, 0.3}};
        sceneSpec.sceneFrames.push_back (movableBox);

        QStringList sceneErrors;
        if (!RobotModelXmlWriter::validate (sceneSpec, sceneErrors))
            return fail ("Scene frame spec should validate: " + sceneErrors.join ("; "));

        const QString scene = RobotModelXmlWriter::makeSceneXml (sceneSpec);
        if (!contains (scene, "<Frame name=\"RobotBase\" refframe=\"WORLD\" type=\"Fixed\">"))
            return fail ("Scene XML should contain RobotBase frame (as Fixed).");
        if (!contains (scene, "<RPY>0 0 90</RPY>\n    <Pos>0.4 -0.2 0.75</Pos>"))
            return fail ("RobotBase pose should be written to Scene XML.");
        if (!contains (scene, "<Frame name=\"Table\" refframe=\"WORLD\" type=\"Fixed\">"))
            return fail ("Scene XML should contain fixed Table frame.");
        if (!contains (scene,
                       "<Frame name=\"Workpiece\" refframe=\"Table\" type=\"Fixed\" daf=\"true\">"))
            return fail ("Scene XML should write daf=true on Workpiece.");
        if (!contains (scene, "<Frame name=\"CameraFrame\" refframe=\"RobotBase\">"))
            return fail ("Normal scene frame should omit type attribute.");
        if (!contains (scene,
                       "<Frame name=\"MovableBox\" refframe=\"WORLD\" type=\"Movable\" daf=\"true\">"))
            return fail ("Movable scene frame with DAF should be written.");
        if (!contains (scene, "<Include file=\"GenericSixAxis.wc.xml\" />"))
            return fail ("Scene XML should still include the robot file.");

        RobotModelSpec badRef = sceneSpec;
        badRef.sceneFrames[0].refFrame = "MissingFrame";
        QStringList badRefErrors;
        if (RobotModelXmlWriter::validate (badRef, badRefErrors))
            return fail ("Scene frame with missing refframe should fail validation.");
        if (!badRefErrors.join (" ").contains ("MissingFrame"))
            return fail ("Missing refframe validation error should mention the bad reference.");
    }

    // ---- Milestone 3.5:默认场景几何体 ----
    {
        RobotModelSpec sceneGeo =
            RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        const QString scene = RobotModelXmlWriter::makeSceneXml (sceneGeo);
        if (!contains (scene, "<Drawable name=\"TableTop\" refframe=\"Table\" colmodel=\"Enabled\">"))
            return fail ("Scene XML should contain a visible/collision TableTop drawable.");
        if (!contains (scene, "<Box x=\"1.2\" y=\"0.8\" z=\"0.05\" />"))
            return fail ("TableTop should be emitted as a Box.");
        if (!contains (scene, "<Drawable name=\"WorkpieceBox\" refframe=\"Workpiece\" colmodel=\"Enabled\">"))
            return fail ("Scene XML should contain WorkpieceBox geometry.");
        if (!contains (scene, "<Drawable name=\"MovableBoxGeom\" refframe=\"MovableBox\" colmodel=\"Enabled\">"))
            return fail ("Scene XML should contain MovableBox geometry.");
    }

    // ---- Milestone 3.5:场景几何校验(refframe / size)----
    {
        RobotModelSpec badGeoRef =
            RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        badGeoRef.sceneGeometries[0].refFrame = "MissingSceneFrame";
        QStringList geoErrors;
        if (RobotModelXmlWriter::validate (badGeoRef, geoErrors))
            return fail ("Scene geometry with missing refframe should fail validation.");
        if (!geoErrors.join (" ").contains ("MissingSceneFrame"))
            return fail ("Scene geometry refframe error should mention MissingSceneFrame.");

        RobotModelSpec badGeoSize =
            RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
        badGeoSize.sceneGeometries[0].size = {{0, 0.8, 0.05}};
        QStringList sizeErrors;
        if (RobotModelXmlWriter::validate (badGeoSize, sizeErrors))
            return fail ("Scene geometry with zero Box size should fail validation.");
    }

    // ---- 把生成的 XML 落到 temp 目录,方便人工核对 ----
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

    // 把"斜向关节"模型也落到磁盘,方便单独对比
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
